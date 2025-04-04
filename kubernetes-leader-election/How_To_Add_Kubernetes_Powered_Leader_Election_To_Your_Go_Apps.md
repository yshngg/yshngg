# How to add Kubernetes-powered leader election to your Go apps

2024-07-19

The Kubernetes standard library is full of gems, hidden away in many of the various subpackages that are part of the ecosystem. One such example that I discovered recently [k8s.io/client-go/tools/leaderelection](https://pkg.go.dev/k8s.io/client-go/tools/leaderelection), which can be used to add a leader election protocol to any application running inside a Kubernetes cluster. This article will discuss what leader election is, how it's implemented in this Kubernetes package, and provide an example of how we can use this library in our own applications.

## Leader Election

Leader election is a distributed systems concept that is a core building block of highly-available software. It allows for multiple concurrent processes to coordinate amongst each other and elect a single "leader" process, which is then responsible for performing synchronous actions like writing to a data store.

This is useful in systems like distributed databases or caches, where multiple processes are running to create redundancy against hardware or network failures, but can't write to storage simultaneously to ensure data consistency. If the leader process becomes unresponsive at some point in the future, the remaining processes will kick off a new leader election, eventually picking a new process to act as the leader.

Using this concept, we can create highly-available software with a single leader and multiple standby replicas.

In Kubernetes, the [controller-runtime](https://github.com/kubernetes-sigs/controller-runtime) package uses [leader election](https://sdk.operatorframework.io/docs/building-operators/golang/advanced-topics/#leader-election) to make controllers highly-available. In a controller deployment, resource reconciliation only occurs when a process is the leader, and other replicas are waiting on standby. If the leader pod becomes unresponsive, the remaining replicas will elect a new leader to perform subsequent reconciliations and resume normal operation.

## Kubernetes Leases

This library uses a [Kubernetes Lease](https://kubernetes.io/docs/concepts/architecture/leases/), or distributed lock, that can be obtained by a process. Leases are native Kubernetes resources that are held by a single identity, for a given duration, with a renewal option. Here's an example spec from the docs:

```
apiVersion: coordination.k8s.io/v1
kind: Lease
metadata:
  labels:
	apiserver.kubernetes.io/identity: kube-apiserver
	kubernetes.io/hostname: master-1
  name: apiserver-07a5ea9b9b072c4a5f3d1c3702
  namespace: kube-system
spec:
  holderIdentity: apiserver-07a5ea9b9b072c4a5f3d1c3702_0c8914f7-0f35-440e-8676-7844977d3a05
  leaseDurationSeconds: 3600
  renewTime: "2023-07-04T21:58:48.065888Z"

```

Leases are used by the k8s ecosystem in three ways:

1. **Node Heartbeats**: Every Node has a corresponding Lease resource and updates its `renewTime` field on an ongoing basis. If a Lease's `renewTime` hasn't been updated in a while, the Node will be tainted as not available and no more Pods will be scheduled to it.
2. **Leader Election**: In this case, a Lease is used to coordinate among multiple processes by having a leader update the Lease's `holderIdentity`. Standby replicas, with different identities, are stuck waiting for the Lease to expire. If the Lease does expire, and is not renewed by the leader, a new election takes place in which the remaining replicas attempt to take ownership of the Lease by updating its `holderIdentity` with their own. Since the Kubernetes API server disallows updates to stale objects, only a single standby node will successfully be able to update the Lease, at which point it will continue execution as the new leader.
3. **API Server Identity**: Starting in v1.26, as a beta feature, each `kube-apiserver` replica will publish its identity by creating a dedicated Lease. Since this is a relatively slim, new feature, there's not much else that can be derived from the Lease object aside from how many API servers are running. But this does leave room to add more metadata to these Leases in future k8s versions.

Now let's explore this second use case of Leases by writing a sample program to demonstrate how you can use them in leader election scenarios.

## Example Program

In this code example, we are using the `leaderelection` package to handle the leader election and Lease manipulation specifics.

```
package main

import (
	"context"
	"fmt"
	"os"
	"time"

	"k8s.io/client-go/tools/leaderelection"
	rl "k8s.io/client-go/tools/leaderelection/resourcelock"
	ctrl "sigs.k8s.io/controller-runtime"
)

var (
	// lockName and lockNamespace need to be shared across all running instances
	lockName      = "my-lock"
	lockNamespace = "default"

	// identity is unique to the individual process. This will not work for anything,
	// outside of a toy example, since processes running in different containers or
	// computers can share the same pid.
	identity      = fmt.Sprintf("%d", os.Getpid())
)

func main() {
	// Get the active kubernetes context
	cfg, err := ctrl.GetConfig()
	if err != nil {
		panic(err.Error())
	}

	// Create a new lock. This will be used to create a Lease resource in the cluster.
	l, err := rl.NewFromKubeconfig(
		rl.LeasesResourceLock,
		lockNamespace,
		lockName,
		rl.ResourceLockConfig{
			Identity: identity,
		},
		cfg,
		time.Second*10,
	)
	if err != nil {
		panic(err)
	}

	// Create a new leader election configuration with a 15 second lease duration.
	// Visit https://pkg.go.dev/k8s.io/client-go/tools/leaderelection#LeaderElectionConfig
	// for more information on the LeaderElectionConfig struct fields
	el, err := leaderelection.NewLeaderElector(leaderelection.LeaderElectionConfig{
		Lock:          l,
		LeaseDuration: time.Second * 15,
		RenewDeadline: time.Second * 10,
		RetryPeriod:   time.Second * 2,
		Name:          lockName,
		Callbacks: leaderelection.LeaderCallbacks{
			OnStartedLeading: func(ctx context.Context) { println("I am the leader!") },
			OnStoppedLeading: func() { println("I am not the leader anymore!") },
			OnNewLeader:      func(identity string) { fmt.Printf("the leader is %s\n", identity) },
		},
	})
	if err != nil {
		panic(err)
	}

	// Begin the leader election process. This will block.
	el.Run(context.Background())

}

```

What's nice about the `leaderelection` package is that it provides a callback-based framework for handling leader elections. This way, you can act on specific state changes in a granular way and properly release resources when a new leader is elected. By running these callbacks in separate goroutines, the package takes advantage of Go's strong concurrency support to efficiently utilize machine resources.

### Testing it out

To test this, lets spin up a test cluster using [kind](https://kind.sigs.k8s.io/).

```
$ kind create cluster

```

Copy the sample code into `main.go`, create a new module (`go mod init leaderelectiontest`) and tidy it (`go mod tidy`) to install its dependencies. Once you run `go run main.go`, you should see output like this:

```
$ go run main.go
I0716 11:43:50.337947     138 leaderelection.go:250] attempting to acquire leader lease default/my-lock...
I0716 11:43:50.351264     138 leaderelection.go:260] successfully acquired lease default/my-lock
the leader is 138
I am the leader!

```

The exact leader identity will be different from what's in the example (138), since this is just the PID of the process that was running on my computer at the time of writing.

And here's the Lease that was created in the test cluster:

```
$ kubectl describe lease/my-lock
Name:         my-lock
Namespace:    default
Labels:       <none>
Annotations:  <none>
API Version:  coordination.k8s.io/v1
Kind:         Lease
Metadata:
  Creation Timestamp:  2024-07-16T15:43:50Z
  Resource Version:    613
  UID:                 1d978362-69c5-43e9-af13-7b319dd452a6
Spec:
  Acquire Time:            2024-07-16T15:43:50.338049Z
  Holder Identity:         138
  Lease Duration Seconds:  15
  Lease Transitions:       0
  Renew Time:              2024-07-16T15:45:31.122956Z
Events:                    <none>

```

See that the "Holder Identity" is the same as the process's PID, 138.

Now, let's open up another terminal and run the same `main.go` file in a separate process:

```
$ go run main.go
I0716 11:48:34.489953     604 leaderelection.go:250] attempting to acquire leader lease default/my-lock...
the leader is 138

```

This second process will wait forever, until the first one is not responsive. Let's kill the first process and wait around 15 seconds. Now that the first process is not renewing its claim on the Lease, the `.spec.renewTime` field won't be updated anymore. This will eventually cause the second process to trigger a new leader election, since the Lease's renew time is older than its duration. Because this process is the only one now running, it will elect itself as the new leader.

```
the leader is 604
I0716 11:48:51.904732     604 leaderelection.go:260] successfully acquired lease default/my-lock
I am the leader!

```

If there were multiple processes still running after the initial leader exited, the first process to acquire the Lease would be the new leader, and the rest would continue to be on standby.

## No single-leader guarantees

This package is not foolproof, in that it ["does not guarantee that only one client is acting as a leader (a.k.a. fencing)"](https://pkg.go.dev/k8s.io/client-go/tools/leaderelection). For example, if a leader is paused and lets its Lease expire, another standby replica will acquire the Lease. Then, once the original leader resumes execution, it will think that it's still the leader and continue doing work alongside the newly-elected leader. In this way, you can end up with two leaders running simultaneously.

To fix this, a [fencing token](https://martin.kleppmann.com/2016/02/08/how-to-do-distributed-locking.html) which references the Lease needs to be included in each request to the server. A fencing token is effectively an integer that increases by 1 every time a Lease changes hands. So a client with an old fencing token will have its requests rejected by the server. In this scenario, if an old leader wakes up from sleep and a new leader has already incremented the fencing token, all of the old leader's requests would be rejected because it is sending an older (smaller) token than what the server has seen from the newer leader.

Implementing fencing in Kubernetes would be difficult without modifying the core API server to account for corresponding fencing tokens for each Lease. However, the risk of having multiple leader controllers is somewhat mitigated by the k8s API server itself. Because updates to stale objects are rejected, only controllers with the most up-to-date version of an object can modify it. So while we could have multiple controller leaders running, a resource's state would never regress to older versions if a controller misses a change made by another leader. Instead, reconciliation time would increase as both leaders need to refresh their own internal states of resources to ensure that they are acting on the most recent versions.

Still, if you're using this package to implement leader election using a different data store, this is an important caveat to be aware of.

## Conclusion

Leader election and distributed locking are critical building blocks of distributed systems. When trying to build fault-tolerant and highly-available applications, having tools like these at your disposal is critical. The Kubernetes standard library gives us a battle-tested wrapper around its primitives to allow application developers to easily build leader election into their own applications.

While use of this particular library does limit you to deploying your application on Kubernetes, that seems to be the way the world is going recently. If in fact that is a dealbreaker, you can of course fork the library and modify it to work against any ACID-compliant and highly-available datastore.

Stay tuned for more k8s source deep dives!
