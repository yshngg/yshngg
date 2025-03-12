# **Node Problem Detector**

*Status: Draft*  
*Authors: lantaol@google.com*  
*Last Updated: 2016-06-06*

[Objective](#objective)  
[Background](#background)  
[Motivation](#motivation)  
[Requirements](#requirements)  
[Overview](#overview)  
[Key Concepts](#key-concepts)  
[v0 (Kubernetes v1.3)](#v0-\(kubernetes-v1.3\))  
[Future (Kubernetes v1.4 and beyond)](#future-\(kubernetes-v1.4-and-beyond\))  
[OutOfScope](#outofscope)  
[System Diagram](#system-diagram)  
[Detailed Design](#detailed-design)  
[API](#api)  
[Problem Type](#problem-type)  
[NodeCondition](#nodecondition)  
[Event](#event)  
[Architecture](#architecture)  
[Proposals](#proposals)  
[Deployment](#deployment)  
[Proposals](#proposals-1)  
[Report Pipeline](#report-pipeline)  
[Problem Daemon \-\> NodeProblemDetector](#problem-daemon--\>-nodeproblemdetector)  
[NodeProblemDetector \-\> APIServer](#nodeproblemdetector--\>-apiserver)  
[Problem Report Interface](#problem-report-interface)  
[LogMonitor](#logmonitor)  
[Project Information](#project-information)

# **Objective** {#objective}

NodeProblemDetector aims to make node problems visible to the control-plane. NodeProblemDetector will collect problems from various node problem daemons and report them to the control-plane. NodeProblemDetector will be easy to integrate with third-party node problem daemon.

# **Background** {#background}

## **Motivation** {#motivation}

There are tons of problems could happen on a node such as hardware problems like bad cpu, memory and disk; kernel problems like kernel deadlock and corrupted file system; container runtime problems like unresponsive runtime daemon etc. Many of them could make the node unavailable for pods.  
Currently these problems are invisible to Kubernetes. Even when there are critical node problems, Kubernetes will continue scheduling pods to the bad nodes. This has caused a lot of issues: [\#19986](https://github.com/kubernetes/kubernetes/issues/19986#issuecomment-174141729), [\#20096](https://github.com/kubernetes/kubernetes/issues/20096), [\#23327](https://github.com/kubernetes/kubernetes/issues/23327), [\#24295](https://github.com/kubernetes/kubernetes/issues/24295) etc.  
As a first step of solving this problem, we need to detect node problems and make them visible to the control-plane. Once the control-plane has the visibility to those problems, we can discuss the remedy system.  
The existing node daemons of Kubernetes are focusing on different aspects, but none of them have been or should be able to handle node problem detection:

* Kubelet: Kubelet is the primary “node agent” on each node. It is mainly responsible for pod lifecycle management.  
* Kube-proxy: Kube-proxy is network proxy on each node. It is mainly responsible for service network configuration.  
* cAdvisor: cAdvisor is responsible for container resource monitoring.

Therefore we need to introduce a new node daemon which is focusing on node health monitoring and node problem detection \- *NodeProblemDetector*.

## **Requirements** {#requirements}

As an open source container orchestration platform, people may run Kubernetes in heterogeneous environment.

* **Extensibility:** People may run Kubernetes on bare metal or VMs, with different hardware, different os distros, different init systems, different container runtimes, etc. It is impossible for a single daemon to handle all these scenarios. So *extensibility* becomes the top requirement for NodeProblemDetector. It must be easy to integrate with different problem daemons so that people can run it with a combination of daemons dedicated to their environment.  
* **Stability:** The problem detector itself should be stable and try best to survive from node problems.  
* **Scalability:** NodeProblemDetector will run on every node. It should put minimum pressure on apiserver and use minimum compute resource on the node.  
* **Security:** Node problems will be shown to users and may change system behaviour like scheduling decision in the future. It should be guaranteed that only qualified daemon could report node problems.   
* **Manageability:** Even in the same cluster, the cluster could be hybrid and the nodes could be heterogeneous. People may want to run special health monitor on master node, GPU health monitor on GPU nodes, windows health monitor on Windows nodes etc. So it should be possible for users to deploy different problem daemons on different nodes.

# **Overview** {#overview}

## **Key Concepts** {#key-concepts}

* **Node Problem:** Node problems is the problem occurs to the node that may affect Kubernetes stability, includes:  
  * Hardware problem. (Bad cpu, bad memory, bad disk, bad gpu etc.)  
  * Kernel problem. (Kernel deadlock, corrupted file systems etc.)  
  * Runtime issue. (Unresponsive docker daemon, zombie docker daemon etc.)  
  * Other problems. (Other problems need attention in the future)  
* **Problem Daemon:** Problem daemon monitors a specific kind of node problems, such as disk problem monitor, kernel problem monitor, runtime problem monitor etc. The problem daemon:  
  * could be tools dedicated for Kubernetes’ use case.  
  * could also be an existing node health monitoring solution.

## **v0 (Kubernetes v1.3)** {#v0-(kubernetes-v1.3)}

* API: NodeProblemDetector will use NodeCondition and Event to report node problems to apiserver. A transient problem will be reported as an Event, and a permanent problem as a NodeCondition.  
* Pipeline:   
  * Each problem daemon will be a separate goroutine in NodeProblemDetector, and report problems to NodeProblemDetector via channel.  
  * NodeProblemDetector will consolidate events and conditions from different problem daemons and synchronize with apiserver.  
* Deployment: NodeProblemDetector and problem daemons will be shipped in a single binary. It will be an [add-on](https://github.com/kubernetes/kubernetes/tree/master/cluster/addons) [DaemonSet](http://kubernetes.io/docs/admin/daemons/) running on each node.  
* KernelMonitor: Introduce KernelMonitor as the first problem daemon for both Kubernetes’ urgent requirement and demonstration purpose. KernelMonitor will be monitoring kernel log and report kernel problems according to predefined rules.

## **Future (Kubernetes v1.4 and beyond)** {#future-(kubernetes-v1.4-and-beyond)}

* Pipeline: Expose problem report interface from NodeProblemDetector, so that other problem daemons could integrate with it. After this, work with the community partners to integrate their problem daemons with Kubernetes.  
* Deployment: Run different problem daemons as separate containers in the NodeProblemDetector DaemonSet.  
* LogMonitor: Generalize the KernelMonitor to be a general purpose log monitor, and use it to detect some simple but critical Docker issues.  
* ControlPlane: Introduce RemedyController to the ecosystem to convert problem related Events and NodeConditions into [Taint](https://github.com/kubernetes/kubernetes/blob/master/docs/design/taint-toleration-dedicated.md), so as to inform scheduler to stop scheduling new pods to bad node and reschedule existing pods to other nodes. 

## **OutOfScope** {#outofscope}

* NodeProblemDetector only makes it possible to integrate node problem daemons with Kubernetes. The responsibility of problem detection should be taken by specific node problem daemons.  
* Different node problem daemons remain owned by their respective vendors, Kubernetes node team will consult and support integrating the node problem daemons with NodeProblemDetector.   
* KernelMonitor (LogMonitor in the future) only aims to detect known kernel and docker issues which have been proved to be critical to Kubernetes. It won’t become a general purpose kernel or docker problem detector.

## **System Diagram** {#system-diagram}

![][image1]

# **Detailed Design** {#detailed-design}

## **API** {#api}

Today, we’ve already used Event and NodeCondition to report some node problems such as OutOfDisk ([\#4135](https://github.com/kubernetes/kubernetes/issues/4135)), MemoryPressure ([\#21274](https://github.com/kubernetes/kubernetes/pull/21274)), NetworkUnavailable ([\#26415](https://github.com/kubernetes/kubernetes/pull/26415)). And we’ll also introduce a Taint API object ([\#18263](https://github.com/kubernetes/kubernetes/pull/18263)) indicating whether the node is schedulable. They are sufficient for the current use case. Other than introducing new API objects blindly, we’d prefer to consolidate the existing objects instead.  
Therefore NodeProblemDetector will use Event and NodeCondition to report node problems to apiserver.

### **Problem Type** {#problem-type}

Based on the severity, node problems are currently divided into 2 categories:

* **Permanent:** Problems that impact long enough that the node is unavailable for hosting some kind of pods. NodeProblemDetector will report permanent problem as a NodeCondition.  
* **Temporary:** Problems that have limited impact on pod hosting, but are informative and useful. NodeProblemDetector will report temporary problem as an Event.

### **NodeCondition** {#nodecondition}

Following is the current [NodeCondition](https://github.com/kubernetes/kubernetes/blob/v1.3.0-alpha.4/pkg/api/types.go#L1894-L1901) api object:

| type NodeCondition struct {	Type               NodeConditionType	Status             ConditionStatus	LastHeartbeatTime  unversioned.Time	LastTransitionTime unversioned.Time	Reason             string	Message            string} |
| :---- |

* Type: Type indicates the name of the bad node condition caused by the problem.  
  * Such as BadCPU, KernelDeadlock, RuntimeHung etc.  
  * *There should be a predefined list of NodeConditionTypes.* The list could be extended overtime but must be under control. Arbitrary string in Type field will make it hard for RemedyController to consume.  
* Reason: Reason is a brief string describing the problem.  
  * It is defined by problem daemons.  
  * It is meant for machine parsing and CLI display, so it should be well-formatted:   
    * CamelCase  
    * Length limit (?)  
    * Convention (?)  
* Message: Message is a string describing the detailed information of the problem.  
  * It is also defined by problem daemons.  
  * It could be an arbitrary string, but length limit (?) is needed in consideration of CLI display and apiserver storage.

Open issues: 

* Do we need problem source? How to expose that in the NodeCondition?  
  * Option 1: We could add source field in NodeCondition  
  * Option 2: We could add problem source as prefix of the Reason, but that would be less straightforward.  
* Should we remove NodeCondition when Status is false?  
  * No. Discussed in [\#25773](https://github.com/kubernetes/kubernetes/issues/25773)

### **Event** {#event}

Following is the current [Event](https://github.com/kubernetes/kubernetes/blob/v1.3.0-alpha.4/pkg/api/types.go#L2255-L2286) api object:

| type Event struct {	InvolvedObject ObjectReference	Reason string	Message string	Source EventSource	FirstTimestamp unversioned.Time	LastTimestamp unversioned.Time	Count int32	Type string } |
| :---- |

* InvolvedObject: InvovledObject is set to Node ObjectReference.  
* Source: Name of the source problem daemon.  
* Reason & Message: The same with NodeCondition.  
* Type: Type is severity of the problem. Now there are only *Normal* and *Warning*, at least we should add *Error (?)*.

## **Architecture** {#architecture}

### **Proposals** {#proposals}

**Alternative 1: NodeProblemDetector gathers problems from problem daemons and synchronizes with apiserver. *(current proposal)***  
NodeProblemDetector is the centralized daemon, gathers node problems from all problem daemons on the node, consolidates the information and synchronizes with apiserver.

* Pros:  
  * Better scalability.  
    * Only NodeProblemDetector communicates with apiserver directly:  
      * Less connection.  
      * Centralized QPS control.  
    * NodeProblemDetector could gather and merge NodeConditions and update node status at once.  
      * Less QPS  
  * Better quality control. NodeProblemDetector could validate problem daemons and problems reported.  
* Cons:  
  * Complexity.  
    * Maintain api between NodeProblemDetector and problem daemons.   
    * Communication mechanism between NodeProblemDetector and problem daemons.

**Alternative 2: Different problem daemons directly talk with apiserver.**  
No centralized daemon needed, each problem daemon directly talks with apiserver.

* Pros:  
  * Simple. Let the problem daemons use api library directly.  
  * Extensible. Anyone could report problems by reporting Event and updating NodeConditions without touching any existing code.  
* Cons:  
  * Worse scalability.  
    * Many daemons watch and write the same Node object.  
    * Daemons control their own QPS.  
    * More QPS needed. Each daemon updates their own NodeCondition separately.  
  * Worse quality control.  
    * All problem detecting and reporting logics are in different problem daemons. Some of them are even not in our code base.  
    * Hard to control api library version used by different problem daemons.  
  * Worse debugability. Multiple daemon could update the same NodeStatus, it would be even harder to debug. ([related issue](https://github.com/kubernetes/node-problem-detector/issues/9))

## **Deployment** {#deployment}

Deployment indicates how NodeProblemDetector and problem daemons are deployed on the node. The following proposals assume NodeProblemDaemon running as the centralized daemon ([Architecture Alternative 1](#proposals))

### **Proposals** {#proposals-1}

**Alternative 1: Run NodeProblemDetector and problem daemons as different containers in the same pod. *(current proposal)***  
**Alternative 2: Run NodeProblemDetector as native process, problem daemons as pods.**

* Pros: (vs. Alternative 1\)  
  * No dependency on Docker daemon. If run NodeProblemDetector in a container, once docker daemon is restarted by supervisord because of node problem, NodeProblemDetector will also be restarted. There is a [chicken-egg issue](https://github.com/kubernetes/kubernetes/issues/26042#issuecomment-221739124). However, docker 1.12 will support [docker daemon hot upgrade](https://github.com/docker/docker/issues/2658), which could help.  
* Cons: (vs. Alternative 1\)  
  * Complexity:  
    * Bootstrap. Another native process to deploy on each node.  
    * Resource usage limit. [NodeAllocable](https://github.com/kubernetes/kubernetes/issues/13984).  
    * Security. Need to expose native process to problem daemon pods on the same node.

**Alternative 3: Run NodeProblemDetector as part of kubelet, problem daemons as pods.**

* Pros: (vs. Alternative 1\)  
  * Same with Alternative 2\.  
* Cons: (vs. Alternative 1\)  
  * Security. Need to expose kubelet to problem daemon pods on the same node.  
  * Unreliable: Kubelet takes more responsibility ≈ less reliability.

**Alternative 4: Run NodeProblemDetector and problem daemons in the same container.**

* Cons: (vs. Alternative 1\)  
  * Need process management in the container.  
  * Can not update separate problem daemons.  
  * Can not specify resource usage for different problem daemons.  
  * … Almost all cons for non-containerized application.

**Alternative 5: Run NodeProblemDetector and problem daemons as different pods.**

* Pros: (vs. Alternative 1\)  
  * Flexible deployment. Different problem daemons could be deployed separately. It’s easy to deploy specific problem daemon on specific kind of nodes.  
* Cons: (vs. Alternative 1\)  
  * Communication between pods on the same node is not as simple as that between containers in the same pod.  
  * Pod management overhead is higher than container management.  
  * Flexible deployment also means hard to deploy. NodeProblemDetector and different problem daemons are deployed separately.

## **Report Pipeline** {#report-pipeline}

For [Architecture Alternative 1](#proposals), we need to design the report pipeline from problem daemons to NodeProblemDetector and then to apiserver.

### **Problem Daemon \-\> NodeProblemDetector** {#problem-daemon-->-nodeproblemdetector}

All the following proposals assume that NodeProblemDetector and problem daemons are running as different containers in the same pod ([Deployment Alternative 1](#proposals-1)). For other deployment approaches, these proposals are also applicable, but may be more complex.  
**Alternative 1: Problem daemon pushes problem to NodeProblemDetector.**  
Define problem reporting interface. NodeProblemDetector exposes problem reporting interface from the endpoint, and problem daemons push problems to the endpoint.

* Pros:  
  * Extensibility. Any daemon implement the interface could integrate without changing NodeProblemDetector.  
* Cons:  
  * Complexity: Need flow control to avoid NodeProblemDetector being flooded or consuming too much resource.  
* **Option 1: Volume based endpoint.**  
  * Specify a volume as the endpoint. Each problem daemon writes problems to the volume. NodeProblemDetector monitors the volume to retrieve problems.  
* **Option 2: Network based endpoint. *(current proposal)***  
  * NodeProblemDetector exposes known network endpoint. Problem daemons report problem to the endpoint.

**Alternative 1.2: NodeProblemDetector pulls problems from Problem daemon.**  
Define problem fetching interface. Each problem daemon exposes problem fetching interface from endpoint, and NodeProblemDetector periodically pulls problems from problem daemons.

* Pros:  
  * NodeProblemDetector controls the poll interval.  
* Cons:  
  * Overhead: If NodeProblemDetector polls frequently, the overhead is higher.  
  * Latency: If NodeProblemDetector polls infrequently, the latency is higher.  
  * Complexity: NodeProblemDetector needs to be configured to know endpoints of all problem daemons.  
* **Option 1: Plugin in NodeProblemDetector.**  
  * NodeProblemDetector defines Go interface, implement different plugins to fetch and convert data from different problem daemons.  
  * Cons: Less extensible. Need to change and rebuild NodeProblemDetector image for each new problem daemon support.  
* **Option 2: Interface from problem daemon.**  
  * NodeProblemDetector defines RESTful interface, each problem daemon has a sidecar implementing the interface.

### **NodeProblemDetector \-\> APIServer** {#nodeproblemdetector-->-apiserver}

NodeProblemDetector uses Kubernetes api client library to report events and node conditions.  
For events, it uses [event recorder](https://github.com/kubernetes/kubernetes/blob/master/pkg/client/record/event.go).  
For node conditions, it needs to update the node conditions once changed, and also needs to avoid the node conditions being modified by other components. There are 2 options:

* **Option 1: Watch+Patch.**  
  * [**The reason why Patch is necessary**](https://github.com/kubernetes/node-problem-detector/issues/9)**.**  
  * Pros: Watch can make sure that once the node conditions are modified, NodeProblemDetector will be informed, so that it could change the conditions back as soon as possible.  
  * Cons: NodeProblemDetector runs as daemon on every node, an extra Watch from every node is a relatively expensive.  
* **Option 2: Get+Patch. *(current proposal)***  
  * Pros: Periodically Get is much more lightweight than Watch.  
  * Cons: During the Get interval, the condition could still be changed for a long time.

Mostly different components should take in charge of different node conditions, a condition should seldom be written by multiple components, so Option 2 is a better choice.

## **Problem Report Interface** {#problem-report-interface}

Problem report interface is the interface between NodeProblemDetector and problem daemons. The following interface definition assumes problem daemons will push problem to NodeProblemDetector ([Report Pipeline Alternative 1](#problem-daemon--\>-nodeproblemdetector)).

| // Severity is the severity of the problem event. Now we only have 2 severity  // levels: Info and Warn, which are corresponding to the current kubernetes event // types. We may want to add more severity levels in the future.type Severity stringconst (	// Info is translated to a normal event.	Info Severity \= "info"	// Warn is translated to a warning event.	Warn Severity \= "warn")// Condition is the node condition used internally by problem detector.type Condition struct {	// Type is the condition type. It should describe the condition of node in 	// problem. For example KernelDeadlock, OutOfResource etc.	Type string \`json:"type"\`	// Status indicates whether the node is in the condition or not.	Status bool \`json:"status"\`	// Transition is the time when the node transits to this condition.	Transition time.Time \`json:"transition"\`	// Reason is a short reason of why node goes into this condition.	Reason string \`json:"reason"\`	// Message is a human readable message of why node goes into this condition.	Message string \`json:"message"\`}// Event is the event used internally by node problem detector.type Event struct {	// Severity is the severity level of the event.	Severity Severity \`json:"severity"\`	// Timestamp is the time when the event is generated.	Timestamp time.Time \`json:"timestamp"\`	// Reason is a short reason of why the event is generated.	Reason string \`json:"reason"\`	// Message is a human readable message of why the event is generated.	Message string \`json:"message"\`}// Status is the status problem daemons should report to node problem detector.type Status struct {	// Source is the name of the problem daemon.	Source string \`json:"source"\`	// Events are temporary node problem events. If the status is only a 		     // condition update, this field could be nil. Notice that the events 	// should be sorted from oldest to newest.	Events \[\]Event \`json:"events"\`	// Conditions are the permanent node conditions. The problem daemon 	// should always report the newest node conditions in this field.	Conditions \[\]Condition \`json:"conditions"\`} |
| :---- |

The server side implementation of the interface is simple. The client side needs a little more design:

* **Periodic Heartbeat.** Even though there is no node problem, the client should periodically report Status with the newest conditions to inform NodeProblemDetector of its aliveness.  
* **Rate Limit.** When there are problems, the client should accumulate events and conditions for a period of time and do batch report.  
* **Error Handling.** The client should handle update failure. When there is update failure, it should be configured to either discard or retry.

## **LogMonitor** {#logmonitor}

LogMonitor monitors and analyses specified log and report problems according to predefined rules. It is the first problem daemon introduced both for demonstration purpose and Kubernetes’ urgent requirement.  
Kubernetes is suffering from kernel deadlock and docker bug for a long time: [\#19986](https://github.com/kubernetes/kubernetes/issues/19986#issuecomment-174141729), [\#20096](https://github.com/kubernetes/kubernetes/issues/20096) etc. LogMonitor is designed to monitor logs such as kernel log and docker log, identify known issues and report them as node problems.  
Following is the brief architecture of LogMonitor:  
![][image2]

* **Log Monitor:** Log Monitor is the main loop. It gets logs from Log Watcher, stores them in Log Buffer, matches problem patterns in Log Buffer and reports problems if there is a match.  
* **Log Buffer:** Log Buffer buffers latest n lines of logs, and supports regex pattern matching in latest logs.  
* **Log Watcher:** Log Watcher watches specified log file. Once there is a new line, it will translate the log into the internal type and send it to Log Monitor.  
* **Translator:** Translator translates a log line into the internal type. *Translator is pluggable. To support new log format, only a new translator is needed.* The definition of the internal type is quite simple:

| type Log struct { 	Timestamp int64 // microseconds since kernel boot 	Message   string } |
| :---- |

* **Rule:** Rule defines how Log Monitor should interpret the log. The definition of Rule:

| // Type is the type of the problem.type Type stringconst (	// Temp means the problem is temporary, only need to report an event.	Temp Type \= "temporary"	// Perm means the problem is permanent, need to change the node condition.	Perm Type \= "permanent")// Rule describes how log monitor should analyze the log.type Rule struct {	// Type is the type of matched problem.	Type Type \`json:"type"\`	// Condition is the type of the condition the problem triggered. Notice that	// the Condition field should be set only when the problem is permanent, or  	// else the field will be ignored.	Condition string \`json:"condition"\`	// Reason is the short reason of the problem.	Reason string \`json:"reason"\`	// Pattern is the regular expression to match the problem in the log. Notice 	// that the pattern must match to the end of the line.	Pattern string \`json:"pattern"\`} |
| :---- |

By implementing different Translators and defining different rules, LogMonitor can support most kinds of logs and detect problems with stable patterns.

# **Project Information** {#project-information}

Project Location: [https://github.com/kubernetes/node-problem-detector](https://github.com/kubernetes/node-problem-detector)  
Team Mailing List: [kubernetes-node-team](https://groups.google.com/a/google.com/forum/#!forum/kubernetes-node)@

[image1]: <data:image/png;base64,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>

[image2]: <data:image/png;base64,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>