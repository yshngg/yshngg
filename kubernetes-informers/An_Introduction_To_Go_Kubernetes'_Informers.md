# An introduction to Go Kubernetes' informers

Posted on September 8, 2021

This article shows you the tool that the [Kubernetes Go client library](https://pkg.go.dev/k8s.io/client-go/informers)
provides to keep an updated in-memory snapshot of your cluster resources.

In the code examples, we use the following package aliases:

```
import (
    metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
    appsv1 "k8s.io/api/apps/v1"
    corev1 "k8s.io/api/core/v1"
)
```

## Motivation

If your Go program requires fetching information about K8s resources (Services,
ReplicaSets, Pods...), you can use the Kubernetes REST API with the
official K8s Go `client` instance:

```
// gets the information of a given pod in the default namespace
pod, err :=  client.CoreV1().Pods("default").
    Get(context.Background(), "pod-name", v1.GetOptions{})

// gets the information of all the currently existing pods in all the
// namespaces
pods, err := client.CoreV1().Pods(corev1.NamespaceAll).
    List(context.Background(), v1.ListOptions{})
```

(Client configuration/instantiation details are omitted for the sake of brevity).

However, you might want to minimize the number of connections as well as the
latency of fetching the resources' data, so you might want to use the `Watch`
interface to listen for changes in the Kubernetes resources and keep an in-memory
copy of the resources:

```
// ignoring returned error on purpose
watcher, _ := client.CoreV1().Pods(corev1.NamespaceAll).
	Watch(context.Background(), metav1.ListOptions{})
for event := range watcher.ResultChan() {
    pod := event.Object.(*corev1.Pod)
    fmt.Printf("%v pod with name %s\n", event.Type, pod.Name)
}
```

The above code would print something similar to this:

```
ADDED pod with name openshift-controller-manager-operator-6b4884d944-gbj2n
ADDED pod with name installer-3-ip-10-0-131-5.ec2.internal
ADDED pod with name kube-storage-version-migrator-operator-684c8fbd9-fw6p8
ADDED pod with name apiserver-86b697ffcb-424gl
ADDED pod with name kube-controller-manager-ip-10-0-131-5.ec2.internal
ADDED pod with name cluster-autoscaler-operator-558c76fc6-l4xwk
ADDED pod with name node-exporter-wjks6
```

To keep an in-memory copy of all the pods in your cluster, you would need to
check the `event.Type` value (`Added`, `Deleted`, `Modified`...) and then
accordingly update a map that stores the pods data, indexed by a unique field
(e.g. pod namespace+name).

You would need to repeat again an again the same code for all the resources you
might want to keep in memory, including:

- Managing the in-memory storage and the indexing, as well as the concurrent
  access to it, if needed.
- Establishing the connection to watch the different resources, as well as
  managing reconnections.

## Informers to the rescue

To minimize boilerplate and repetitive code, the Kubernetes Go library provides
entities named _Informers_, which continously watch for your Kubernetes resources
updates (additions, deletions, modifications...) and keep an in-memory copy
of them, which can be retrieved by a given index.

You can create informers for each resource type by means of an informer factory.
For example, the following code would create a pods' informer:

```
// resyncing in-memory copy each 10 minutes
factory := informers.NewSharedInformerFactory(client, 10*time.Minute)
podsInformer := factory.Core().V1().Pods().Informer()
```

The following code would start the above informer (and any other informer created
by the factory) and wait until it gets a complete in-memory copy of your Pods:

```
stopCh := make(chan struct{})
factory.Start(stopCh) // runs in background
factory.WaitForCacheSync(stopCh)
```

Even after waiting for the cache synchronization, all the informers created
by the factory would stay in background, updating the memory with any change
in the cluster pods. You can close the `stopCh` to interrupt the background
execution of all the informers.

By default, the informers for namespaced resources store them using the
`namespace/name` string as key. You can retrieve any pod by its namespace and
name in the following way:

```
// ignoring returned ok and err for brevity
podItem, _, _ := podsInformer.GetIndexer().GetByKey(namespace + "/" + name)
pod := podItem.(*corev1.Pod)
fmt.Println("The Pod IP is", pod.Status.PodIP)
```

Observe that, due to the [lack of generics in Go](https://go.googlesource.com/proposal/%2B/refs/heads/master/design/43651-type-parameters.md),
you still need to deal with some `interface{}` types.

Now imagine that, in addition to accessing your pods by name, you would like
to get them indexed by IP address. You can add a new indexer **before you
start the Informer factory**. A new Pod indexer would receive a `*Pod` instance
and can return a list of `string` values that can be used as an index for such Pod. In
our case, the list of IPs for this pod.

```
// arbitrary unique name for the new indexer
const ByIP = "IndexByIP"
podsInformer.AddIndexers(map[string]cache.IndexFunc{
    ByIP: func(obj interface{}) ([]string, error) {
        var ips []string
        for _, ip := range obj.(*corev1.Pod).Status.PodIPs {
            ips = append(ips, ip.IP)
        }
        return ips, nil
    },
})
```

When the informer is started, any new pod will be indexed in two ways: by its
`namespace/name` and by any of its IP addresses.

Now, to retrieve a Pod by its IP, we need to ask it to the new `IndexByIP` index
that we added previously:

```
items, err := podsInformer.GetIndexer().ByIndex(ByIP, ip)
```

Usually, the `items` array would return a zero-length array if there is not
any pod with the passed IP, or a single-item array for most existing Pods.

However, for special cases like [host-networked pods](https://www.alibabacloud.com/help/doc-detail/123997.htm),
which share the same Host IP, would return an array with all the Pods sharing the
same IP.

In addition, you can tell the factory to create Informers for other resources, which
will work analogous to Pods informers:

```
replicaSetInformer := factory.Apps().V1().ReplicaSets().Informer()
servicesInformer := factory.Core().V1().Services().Informer()
```

## Conclusions

- The [Informers from the Go Kubernetes library](https://pkg.go.dev/k8s.io/client-go/informers)
  help us with the boilerplate of having to keep an in-memory copy of our Kubernetes
  resources.
- The Informers library is flexible enough to extend it for our own use cases,
  such as indexing by arbitrary fields, and even providing a different storage
  layer (not shown in this article).
- When Go generics arrive, the Informers API could be improved to
  provide even cleaner code with type safety.
