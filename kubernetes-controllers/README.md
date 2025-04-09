# So you wanna write Kubernetes controllers?

### What they don't tell you about developing scalable and reliable controllers

Published on 22 January 2025 (est. 17 min read)

Any company using Kubernetes eventually starts looking into developing their
custom controllers. After all, what's not to like about being able to provision
resources with declarative configuration: [Control loops](https://youtu.be/zCXiXKMqnuE?t=128) are fun,
and [Kubebuilder](https://kubebuilder.io/) makes it extremely easy to get started with writing Kubernetes
controllers. Next thing you know, customers in production are relying on the
buggy controller you developed without understanding how to design idiomatic
APIs and building reliable controllers.

Low barrier to entry combined with good intentions and the "illusion of
*working* implementation[1](#fn:1)" is not a recipe for
success while developing production-grade controllers. I've seen the real-world
consequences of controllers developed without adequate understanding of
Kubernetes and the controller machinery at multiple large companies. We went
back to the drawing board and rewritten nascent controller implementations a few
times to observe which mistakes people new to controller development
make.

1. [Design CRDs like Kubernetes APIs](#design-crds-like-kubernetes-apis)
2. [Single-responsibility controllers](#single-responsibility-controllers)
3. [Reconcile() method shape](#reconcile-method-shape)
4. [Report `status` and `conditions`](#report-status-and-conditions)
5. [Learn to use `observedGeneration`](#learn-to-use-observedgeneration)
6. [Understand the cached clients](#understand-the-cached-clients)
7. [Fast and offline reconciliation](#fast-and-offline-reconciliation)
8. [Reconcile return values](#reconcile-return-values)
9. [Workqueue/resync mechanics](#workqueueresync-mechanics)
10. [Expectations pattern](#expectations-pattern)
11. [Conclusion](#conclusion)

## Design CRDs like Kubernetes APIs

It takes less than 5 minutes to write a Go struct and generate a Kubernetes
CustomResourceDefinition (CRD) from it thanks to controller-gen. Then it takes
several months to [migrate](https://www.linkedin.com/blog/engineering/infrastructure/how-linkedin-moved-its-kubernetes-apis-to-a-different-api-group) from this poorly designed API to a better v2 design
while the old API is being used in production. Don't do that to yourself.

If you're serious about developing long-lasting production grade controllers,
you have to deeply understand the [API Conventions](https://github.com/kubernetes/community/blob/8a99192b3780b656f9dd53c0c37d9372a1c975f9/contributors/devel/sig-architecture/api-conventions.md) that
Kubernetes uses to design its builtin APIs. Then, you need to study the builtin
APIs, and think about things like "why is this field here", "why is this field
not a boolean", "why is this a list of objects and not a string array". Only
when you're able to reason about the builtin Kubernetes APIs and their design
principles, you'll be able to design a long-lasting custom resource API.

Beginners not grasping these [API conventions](https://github.com/kubernetes/community/blob/8a99192b3780b656f9dd53c0c37d9372a1c975f9/contributors/devel/sig-architecture/api-conventions.md) often make these
mistakes:

1. They don't understand the difference between `status` and `spec` and who
   should be updating each field (more about this later ).
2. They don't understand how to embed a child object within a parent object
   (e.g. how `Deployment.spec.template` becomes a `Pod`) so they end up
   re-creating child object properties in the parent object, usually with a
   worse organized structure.
3. They don't understand field semantics well (e.g. zero values, defaulting,
   validation) and end up with fields that are not set, or set to wrong values
   accepted into the API.
   I covered this topic in my
   [CRD generation pitfalls article](/blog/crd-generation-pitfalls/). If the behavior of the
   API is not clear when a field is not set, you've already failed.
   [API conventions](https://github.com/kubernetes/community/blob/8a99192b3780b656f9dd53c0c37d9372a1c975f9/contributors/devel/sig-architecture/api-conventions.md) guide covers this topic fairly well.

If you study the builtin Kubernetes APIs extensively, you'll find out things like
`spec` field is not a "must have"[2](#fn:2), and not all APIs offer a `status` field.
I would go as far as to say that you should also study custom APIs of projects
like Knative, Istio and other popular controllers to develop a better
understanding of organizing fields, and how to reuse some core types Kubernetes
already offers (like `ControllerRevision`, `PodSpecTemplate`).

## Single-responsibility controllers

Time and time again we find engineers adding new unrelated responsibilities to
existing controllers because it seems like a good place their *thing* can be
shoved into. Kubernetes core controllers don't have this problem for a reason.

One of the main Kubernetes [design principles](https://github.com/kubernetes/design-proposals-archive/blob/acc25e14ca83dfda4f66d8cb1f1b491f26e78ffe/architecture/principles.md#design-principles) is that controllers have clear
inputs and outputs —and they do a well-defined job. For example, the Job
controller watches `Job` objects and creates `Pod`s, which is a clear mental
model to reason about. Similarly, each API is designed to offer a well defined
functionality. A controller's output can be an input to another controller.
This is all what the [UNIX
philosophy](https://en.wikipedia.org/wiki/Unix_philosophy#Origin) suggests for
a well reasoned system.

I recommend studying the common [controller shapes](https://youtu.be/zCXiXKMqnuE?t=539) (great talk by Daniel Smith,
one of the architects of kube-apiserver) and the core Kubernetes controllers.
You'll notice that each core controller in Kubernetes core has a very clear job
and inputs/outputs that can be explained in a small diagram. If your controller
isn't like this, you're probably misarchitecting either your controller, or your
CRDs.

If you architect your APIs and controllers correctly, your controllers will run
in harmony as if they're integrating with Kubernetes core APIs or an
off-the-shelf operator.

When you controller design doesn't quite feel right, or has too many
inputs/outputs, does too much, or in general doesn't *feel right*, you're
probably doing it unidiomatically. I struggled with this a lot myself,
especially while developing controllers that manage external resources that have
a non-declarative configuration paradigm.

## Reconcile() method shape

Assuming you use kubebuilder (which uses [controller-runtime](https://github.com/kubernetes-sigs/controller-runtime) like
almost everyone else to develop a controller, and you implement the
`Reconcile()` method that controller-runtime invokes every time one of your
inputs change. This is where your controller does its magic, and since it's
possible to implement this method in any way, most beginners dump their
spaghetti here.

Therefore, large projects like Knative define their own [common controller
shapes](https://github.com/knative/pkg/tree/accfe36491888e45ce8bd923ff8996283c055ae1/reconciler)
where every controller runs the same set steps in the same order. By developing
a common controller shape/framework, you create a "guardrail" so that other
engineers don't deviate and introduce bugs in the reconciliation flow easily.

Sadly, controller-runtime is not opinionated about this topic. Your best bet is
to read other controllers (like [Cluster
API](https://github.com/kubernetes-sigs/cluster-api)) to learn the idioms and
master the reconciliation flow.

There are also new projects like
[Reconciler.io](https://github.com/reconcilerio/runtime) and [Apollo SDK by
Reddit](https://github.com/reddit/achilles-sdk) that claims to offer
finite-state machines for controller-runtime reconcilers.

Over time, we found that almost all our controllers have a similar
reconciliation flow. Here's a pseudo-code of how our controllers look like:

```
func (r *FooController) Reconcile(..., req ctrl.Request) (ctrl.Result, error) {
    log := ctrl.LoggerFrom(ctx)
    obj := new(apiv1.Foo)
    // 1. Fetch the resource from cache: r.Client.Get(ctx, req.NamespacedName, obj)
    // 2. Finalize object if it's being deleted
    // 3. Add finalizers + r.Client.Update() if missing

    orig := obj.DeepCopy() // take a copy of the object since we'll update it below
    obj.InitializeConditions() // set all conditions to "Unknown", if missing

   // 4. Reconcile the resource and its child resources (the magic happens here).
   //    Calculate the conditions and other "status" fields along the way.
   reconcileErr := r.reconcileKind(ctx, obj) // magic happens here!

    // 5. if (orig.Status != foo.Status): client.Status.Patch(obj)
}

```

Most notably, you'll see here that we always initialize conditions and always
update status inside `reconcileKind` even if the reconciliation fails.

I recommend enforcing a similar common shape for controllers developed at your
company (you can use custom kubebuilder plugins during scaffolding, but you
can't really *enforce* that either).

## Report `status` and `conditions`

I practically never seen a beginner engineer create a CRD that has properly
designed `status` fields (if one exists, at all). Kubernetes [API
conventions](https://github.com/kubernetes/community/blob/8a99192b3780b656f9dd53c0c37d9372a1c975f9/contributors/devel/sig-architecture/api-conventions.md) discuss this at length, so I'll keep it
brief. If an API object is reconciled by a controller, the resource should
expose its status in `status` fields. For example, there's no ConfigMap
controller so ConfigMap doesn't have a `status` field.

At LinkedIn, our custom API objects have a `status.conditions` field, similar to
the Kubernetes core or [Knative
conditions](https://github.com/knative/pkg/blob/accfe36491888e45ce8bd923ff8996283c055ae1/apis/condition_types.go#L58-L85),
and we use something similar to [Knative condition set manager](https://github.com/knative/pkg/blob/accfe36491888e45ce8bd923ff8996283c055ae1/apis/condition_set.go) that provides
high-level accessor methods to set the conditions, and sort them etc.

This helps us define and report conditions for API objects in a high-level way
in the reconciler code:

```
func (r *FooReconciler) reconcileKind(obj *Foo) errror {
    // Create/configure a Bar object, wait for it to get Ready
    if err := r.reconcileBar(obj); if err != nil {
        obj.MarkBarNotReady("couldn't configure the Bar resource: %w", err)
        return fmt.Errorf("failed to reconcile Bar: %w", err)
    }
    obj.MarkBarReady()
}

```

Every time we mark a condition, the condition manager recalculates the top-level
`Ready` condition, which all our objects have as Kubernetes API conventions
suggest. Other controllers and humans consume this top-level condition to
understand how the objects are doing (plus you get to use [`kubectl cond`](https://github.com/ahmetb/kubectl-cond/)
on your objects).

## Learn to use `observedGeneration`

Something notable is that all our `conditions` have an `observedGeneration`
field. You'll even see some popular community CRDs (like ArgoCD
[Application](https://doc.crds.dev/github.com/argoproj/argo-cd/argoproj.io/Application/v1alpha1%40v2.13.3))
do not offer this field.

Essentially, this field tells us whether the condition is calculated based on
the last configuration of the object —or whether we're looking at
a stale status information because the controller hasn't gotten to reconciling
the object after the update.

For example, observing a `Ready` condition set to `True` alone means nothing
(other than at some point in the past it was true). The condition offers a
meaningful status info if and only if the `cond.observedGeneration == metadata.generation`.

**Real-world story:** A controller we had in production didn't have the notion
of `observedGeneration`, so its callers would update the object's `spec` and
immediately check its `Ready` condition. This condition would almost always be
stale, as the controller hadn't reconciled the object yet. So the callers
interpreted an app rollout as *completed*, even though it hadn't even started
yet (and sometimes actually failed, but that failure was never noticed).

## Understand the cached clients

controller-runtime, by default, gives you a client to the Kubernetes API which
serves the reads from an in-memory cache (as it uses [shared informers](https://leftasexercise.com/2019/07/15/understanding-kubernetes-controllers-part-iii-informers/) from
`client-go` under the covers). This is mostly fine, as controllers are designed
to operate on stale data —but it is detrimental if you didn't know this
was the case since you might be writing buggy controllers due to this (more on
this later in "expectations" section).

When you perform a write (which directly hits the API server), its results may
not be immediately visible in the cached client. For example, when you delete an
object, it may still show up in the list result in a subsequent reconciliation
to your surprise.

The lesser-known behavior of controller-runtime most beginners don't realize is
that controller-runtime establishes new informers on-the-fly. Normally, when you
specify explicit event sources while building your controller (e.g. in
`builder.{For,Owns,Watches}`), the informers are started and caches are started
during startup.

However, if you try to make queries with `client.{Get,List}` on resources that
you haven't declared upfront in your controller setup, controller-runtime will
initialize an informer on-the-fly and block on warming up its cache. This leads
to issues like:

* Controller-runtime starting a watch for a resource type and start caching all
  its objects in memory (even if you were trying to query only one resource),
  potentially leading to the process running out of memory.
* Unpredictable reconciliation times while the informer cache is syncing, during
  which your worker goroutine will be blocked from reconciling other resources.

That's why I recommend setting `ReaderFailOnMissingInformer: true` and disabling
this behavior so you're fully aware of what kinds your controller is maintaining
watches/caches on. Otherwise, controller-runtime doesn't provide any
observability on what informers it's maintaining in the process.

controller-runtime offers [a lot of other cache knobs](https://pkg.go.dev/sigs.k8s.io/controller-runtime/pkg/cache#Options), such as entirely
disabling the cache on certain types, dropping some fields from the in-memory
cache, or limit the cache to certain namespaces. I recommend studying them to
better understand how you can customize the cache behavior.

## Fast and offline reconciliation

Reconciling an object that is alredy up-to-date (i.e. goal state == current
state) should be really fast and offline —meaning it should not make any
API calls (to external APIs or writes to Kubernetes API). That's why controllers
use [cached clients](#understand-the-cached-clients) to serve reads from the
cache to determine the state of the world.

I've seen many real-world controllers making API calls to external systems, or
make status updates to Kubernetes API (even when nothing has changed) every time
`Reconcile()` got invoked. This is an anti-pattern, and a really bad idea for
writing scalable and reliable controllers:

1. They bombarded the external APIs with unnecessary calls during controller
   startup (or full resyncs, or when they had bugs causing infinite requeue
   loops)
2. When the external API was down, reconciliation would fail even though
   nothing has changed in the object. Depending on the implementation, this
   can block the next steps in the reconciliation flow even though those steps
   don't depend on this external API call.
3. Logic that takes long to execute in a reconciliation loop will hog the worker
   goroutine, and cause workqueue depth to increase, and reduce the
   throughput/responsiveness of the controller as the worker goroutine is
   occupied with the slow task.

Let's go through a concrete example: Assume you have an S3Bucket controller that
creates and manages S3 buckets using AWS S3 API. If you make a query to S3 API
on every reconciliation, you're doing it wrong. Instead, you should store the
result of the S3 API calls you made, in a field like
`status.observedGeneration`, to reflect what's the last generation of the object
that was successfully conveyed to S3 API. If this field has 0 value, the
controller knows it needs to make a "Create Bucket" call to S3 API. When a
client updates the S3Bucket custom resource, its `metadata.generation` will no
longer match its stored `status.observedGeneration`, so the controller knows it
needs to make a "Update Bucket" call to S3 API, and only upon success it will
update the `status.observedGeneration` field. This way, you avoid making calls
to the external S3 API when the object is already up-to-date.[3](#fn:3)

## Reconcile return values

Your `Reconcile()` function signature returns
[`ctrl.Result`](https://pkg.go.dev/sigs.k8s.io/controller-runtime%40v0.20.0/pkg/reconcile#Result)
+`error` values. Usually beginners don't have a solid grasp on what values to
return from `Reconcile()`.

You should know that your `Reconcile()` function is invoked every time your
event sources declared in `builder.{For,Owns,Watches}` changes. If you know
that, my general advice while returning reconciliation values:

1. If you have `error`s during reconciliation, return the error; not `Requeue: true`. Controller-runtime will requeue for you.
2. Use `Requeue: true` only when there's no error but something you started is
   still in progress, and you want to check its status with the [default backoff
   logic](https://github.com/kubernetes/client-go/blob/9897373fe6348db656b1f4039033d509b8a4f241/util/workqueue/default_rate_limiters.go#L51-L53).
3. Use `RequeueAfter: <TIME>` only when you want to reconcile the object after
   a certain time has passed. This is useful for implementing a wall-clock based
   periodic reconciliation (e.g. a CronJob controller, or you want to retry
   reconciliation at a custom poll interval).

It's a [matter of
preference](https://groups.google.com/g/operator-framework/c/K7zwQiCJVYg/m/03NT2HYeCAAJ)
whether your `Reconcile()` function should make as much progress as possible in
a single run; or, return early every time you change something and requeue
itself again. You'll see the earlier approach is more unit-test friendly and
what you'll find more frequently in the open-source controllers because if your
event triggers are set up correctly, the object will get requeued anyway.

## Workqueue/resync mechanics

OpenKruise has an [article about workqueue mechanics](https://openkruise.io/blog/learning-concurrent-reconciling/), go read
that. I frequently see beginners not relying on assumptions like an object
is guaranteed to be reconciled at the same time in different workers, so they
end up implementing unnecessary locking mechanisms in their controllers.

Similarly, beginners frequently don't understand *when* and *how many times* an
object gets reconciled. For example, when your controller updates the object
it's working on, it'll be requeued for a reconciliation immediately again
(because the update you made triggers watch event).

Even when no objects were updated, all watched resources will be requeued
periodically to get reconciled again (called "resync" configured via
`SyncPeriod` option). This is the default behavior since the controllers may
miss watch events (very rare), or skip processing some events during leadership
change. But this behavior causes you to do a full reconciliation of all objects
cached.[4](#fn:4) So by default, your controller should assume it'll reconcile the
entire world periodically.

**Real-world story:** We had a controller that managed several thousand objects
and it did a full resync every 20 minutes. Every object took several seconds to
reconcile. So any time a client created or updated an object, it would not get
reconciled until many minutes later, as it goes to the back of the workqueue
among. If this happened during full resync or controller startup, it took
many minutes until any work was done on this object.

Starting with controller-runtime v0.20 has
[introduced](https://github.com/kubernetes-sigs/controller-runtime/releases/tag/v0.20.0)
a [priority
queue](https://github.com/alvaroaleman/controller-runtime/blob/bd6eede09a6d6fd3eebb374ee62c9338886a7e13/designs/priorityqueue.md)
implementation for the workqueue. This would deprioritize reconciliation of
objects that were not edge-triggered (i.e. due to an create/update etc.) and
make the controller more responsive during full resyncs and controller startups.

That's why understanding the workqueue semantics, worker count
(`MaxConcurrentReconciles`) and monitoring your controller's reconciliation
latency, workqueue depth and active workers count is super important to know if
your controller scales or not.

## Expectations pattern

We discussed above that controller-runtime client serves the *reads* from an
informer cache, and doesn't query the API server except during the
startup/resyncs.

This cache is kept up-to-date based on the received "watch" events from the API
server. Therefore, your controller will almost certainly read stale data at some
point, since the watch events arrive asynchronously after the writes you make.
Cached clients don't offer [read-your-writes
consistency](https://arpitbhayani.me/blogs/read-your-write-consistency/).

This means you need to program your `Reconcile()` method with this assumption at
all times. This is not at all intuitive, but a reality when you work with
a cached client. I'll give several real-world examples:

**Example 1:** You're implementing the `ReplicaSet` controller. Controller sees
\*a
ReplicaSet with `replicas: 5`, so it lists the pods with `client.List` (which
is served from the cache), and you get 3 Pods. It turns out the informer cache
wasn't up-to-date, but the API actually had 5 pods. Your controller creates 2
more pods, now you have 7 Pods. Definitely not what you wanted.

**Example 2:** Now you're scaling down a ReplicaSet from 5 to 3. You list the
Pods, you see 5 Pods, you delete 2 Pods, and next time you list the Pods again,
you still see, you delete another 2 Pods. If your deletion logic is not
deterministic (e.g. sorting Pods by name), you scaled from 5 to 1
—definitely not what you wanted.

**Example 3:** For every object kind=`A`, you create an object kind=`B`. When
`A` gets updated, you update `B`. The update succeeds, but next time you reconcile
`A` again, you don't see an updated version of `B`, so you update `B` to the
goal state again, and you get a `Conflict` error because you're updating the old
version of the object. But you already updated it, why update again?

If you don't know how to solve these problems in your controller, it's likely
because you haven't seen the "expectations" pattern before.

In this case, controllers need to do in memory bookkeeping of their expectations
that resulted from the successful writes they made.
Once an expectation is recorded, the controller knows it needs to wait for the
cache to catch up (which will trigger another reconciliation), and not do its
job based on the stale result it sees from the cache.

You can see many core controllers [use this
pattern](https://github.com/kubernetes/kubernetes/blob/a882a2bf50e630a9ffccbd02b8f759ea51de1c8f/pkg/controller/controller_utils.go#L119-L132),
and Elastic operator also has a [great
explanation](https://github.com/elastic/cloud-on-k8s/blob/6c1bf954555e5a65a18a17450ccddab46ed7e5a5/pkg/controller/common/expectations/expectations.go#L16-L78)
alongside their implementation. We implemented a couple of variants of these
at LinkedIn ourselves.

## Conclusion

Usually when you have controller development questions, join the Kubernetes
slack and ask in the #controller-runtime channel. The maintainers are very
helpful! If you're looking for a good controller implementation, I recommend
studying the [Cluster API](https://github.com/kubernetes-sigs/cluster-api/)
codebase. Also, Operator SDK has a [best practices
guide](https://sdk.operatorframework.io/docs/best-practices/best-practices/) you
should check out.

I'm not the most experienced person to write a detailed guide on this, but I'll
be writing more about beginner pitfalls and controller development anti-patterns.

At LinkedIn we use a controller development exercise [a former
colleague](https://twitter.com/diptanu) came up with to onboard new engineers to
get them to understand the controller machinery. This exercise touches many
aspects of controller development and gets people familiar with core Kubernetes
APIs:

> **Exercise:**
> Implement a SequentialJob API and controller. The API should
> allow users to specify a series of run-to-completion (batch job) container
> images to run sequentially.
>
> **Follow up questions:**
>
> * *How do users specify the list of containers? (Do you use the core types?)*
> * *Do you report status? How is status calculated? How do you surface job failures?*
> * *Where do you validate user inputs? Where do you report reconciliation failures?*
> * *What happens if the SequentialJob changes while the jobs are running?*
> * *How are the child resources you created cleaned up?*

I hope this article helps you be a better controller developer. If you feel like
this sort of work resonates with you, we're usually hiring nowadays [[1]](https://www.linkedin.com/jobs/view/4118956306/)
[[2](https://www.linkedin.com/jobs/view/4138638859/)] so reach out to me for a referral!

*Thanks to Mike Helmick for reading drafts of this article and giving feedback.*

---

1. In distributed systems, "success" is not an interesting case. A controller
   seeming to work okay is not an indicator of much. The hard work is designing
   for scale and time, understanding failure modes, proving correctness in the edge
   cases. [↩︎](#fnref:1)
2. See APIs like ConfigMap, Secret, ValidatingWebhookConfiguration. [↩︎](#fnref:2)
3. This example assumes the controller doesn't need to periodically sync
   with the external API to correct potentially drifted configuration (e.g.
   updates made out of band). Whether controllers should do this or not entirely
   depends on your business case. Here is a Bluesky thread
   [[1]](https://bsky.app/profile/ahmet.dev/post/3lftj5gnzmk2c)]
   [[2]](https://bsky.app/profile/thock.in/post/3lfuq7huj2s2j) about this topic if
   it interests you. [↩︎](#fnref:3)
4. Note that full periodic resyncs can overload API Servers [prior to
   Kubernetes
   v1.27](https://github.com/kubernetes/enhancements/tree/master/keps/sig-api-machinery/3157-watch-list#motivation)
   (when your controller watches high-cardinality resources like Pods) as the List
   calls are quite expensive in kube-apiserver (although recent versions [addressed
   this](https://github.com/kubernetes/enhancements/blob/master/keps/sig-api-machinery/3157-watch-list/README.md#summary),
   with more improvements [on the
   way](https://github.com/kubernetes/enhancements/issues/4988)). Uber mentioned in
   one of their talks they don't do a full list from kube-apiserver, as the reason
   why some objects may miss reconciliation is that the events get dropped during
   leadership change, not because the watch stream is missing events. So, instead
   of using `SyncPeriod` which does a full List call, they instead requeue all
   objects that are already in cache. [↩︎](#fnref:4)

### Ahmet Alp Balkan

I'm a software engineer at LinkedIn's
Kubernetes-based compute infrastructure team.
In the past, I've worked at Twitter, Google Cloud and
Microsoft Azure on containers and Kubernetes.
In my spare time, I [maintain](https://github.com/ahmetb)
several tools in the Kubernetes open source ecosystem.

[About me](/)
[Other articles](/blog/)
[Follow on Bluesky](https://bsky.app/profile/ahmet.dev)
[Follow on **𝕏**](https://twitter.com/ahmetb)
