#pragma once

#include <vector>
#include <cstdint>
#include <ctime>
#include <map>
#include <set>
#include "Crust/Array.h"
#include "Crust/Optional.h"
#include "Crust/PooledString.h"
#include "Crust/PooledBlob.h"
#include "Crust/BlobStream.h"
#include "Crust/FormattedText.h"


class Task;
typedef std::shared_ptr<Task> TaskPtr;
typedef std::weak_ptr<Task> TaskWeakPtr;
class TaskDatabase;


typedef uint64_t TaskID;


// When a task is run, the command: "<interpreter> <script_file> <args>" will be executed,
// where <script_file> is a file containing the provided script data blob.
struct TaskExecutable
{
    PooledString command; // a command to run in the shell

    void serialize(BlobStreamWriter& writer) const;
    bool deserialize(BlobStreamReader& reader);
};

inline BlobStreamWriter& operator<<(BlobStreamWriter& writer, const TaskExecutable& val) { val.serialize(writer); return writer; }
inline bool operator>>(BlobStreamReader& reader, TaskExecutable& val) { return val.deserialize(reader); }


// This encapsulates all the information on when/where to run a task
struct TaskSchedule
{
    std::vector<PooledString> requiredResources; // required resource tags that workers must have to run this task
    std::vector<PooledString> optionalResources; // optional resource tags that workers are preferred to have to run this task

    uint32_t workerUsage_FixedPoint;
    const uint32_t s_workerUsage_FixedPointMax = 0xFFFF;

    float getWorkerUsageFraction() const { return float(workerUsage_FixedPoint) / float(s_workerUsage_FixedPointMax); }
    void setWorkerUsageFraction(float fraction);

    void serialize(BlobStreamWriter& writer) const;
    bool deserialize(BlobStreamReader& reader);

    std::string toString() const;
};

inline BlobStreamWriter& operator<<(BlobStreamWriter& writer, const TaskSchedule& val) { val.serialize(writer); return writer; }
inline bool operator>>(BlobStreamReader& reader, TaskSchedule& val) { return val.deserialize(reader); }


// These task states are simply conveniences for the user when inspecting a Task object. Internal state is NOT
// stored via a TaskState value, but with Optional<TaskRunStatus> data.
enum class TaskState : uint8_t
{
    Pending, Running, Canceling
};

std::string toString(TaskState state);


// This struct describes status information for tasks that are NOT pending; e.g. either running, or finished.
struct TaskRunStatus
{
    // This does NOT mean the task has finished, just that it was marked for cancellation
    bool wasCanceled;
    // This marks the time the task started running on the worker that dequeued it
    std::time_t startTime;
    // This tracks the last time the worker that is running this task was heard from (used to timeout tasks)
    std::time_t heartbeatTime;

    void serialize(BlobStreamWriter& writer) const;
    bool deserialize(BlobStreamReader& reader);
};

inline BlobStreamWriter& operator<<(BlobStreamWriter& writer, const TaskRunStatus& val) { val.serialize(writer); return writer; }
inline bool operator>>(BlobStreamReader& reader, TaskRunStatus& val) { return val.deserialize(reader); }


// This struct describes the runtime status of a task, i.e. when it was enqueued, when it started running (if it has), etc.
struct TaskStatus
{
    std::time_t createTime; // has no functional effect on task execution
    Optional<TaskRunStatus> runStatus; // if no value exists, then the task is still pending

    TaskState getState() const; // this classifies the task into several disjoint states; see TaskState

    void serialize(BlobStreamWriter& writer) const;
    bool deserialize(BlobStreamReader& reader);

    std::string toString() const;
};

inline BlobStreamWriter& operator<<(BlobStreamWriter& writer, const TaskStatus& val) { val.serialize(writer); return writer; }
inline bool operator>>(BlobStreamReader& reader, TaskStatus& val) { return val.deserialize(reader); }


// This is a simple structure to group together all the information needed to start a task
struct TaskCreateInfo
{
    TaskExecutable executable;
    TaskSchedule schedule;

    void serialize(BlobStreamWriter& writer) const;
    bool deserialize(BlobStreamReader& reader);
};

inline BlobStreamWriter& operator<<(BlobStreamWriter& writer, const TaskCreateInfo& val) { val.serialize(writer); return writer; }
inline bool operator>>(BlobStreamReader& reader, TaskCreateInfo& val) { return val.deserialize(reader); }

class TaskDB;


// Main task class not only provides (private, shared only with TaskDatabase) methods to change task run state 
// information, but tracks additional data about graph connectivity to enable knowing when a task becomes ready 
// in constant time.
class Task : public std::enable_shared_from_this<Task>
{
public:
    Task(TaskID id, const TaskCreateInfo& startInfo);
    
    TaskID getID() const { return m_id; }
    std::string getHexID() const;
    
    const TaskExecutable& getExecutable() const { return m_executable; }
    const TaskSchedule& getSchedule() const { return m_schedule; }
    const TaskStatus& getStatus() const { return m_status; }

private:
    friend class TaskDatabase;

    TaskID m_id;
    TaskExecutable m_executable; // what to execute by the worker
    TaskSchedule m_schedule; // where and when to run the task
    TaskStatus m_status;

    void markStarted();
    bool markShouldCancel();
    void heartbeat();
};


typedef std::map<TaskID, TaskPtr> TasksByID;

struct TaskStats
{
    TaskStats();

    int numPending;
    int numRunning;
    int numCanceling;
    uint64_t numFinished;
};

class TaskDatabase
{
public:
    TaskPtr getTaskByID(TaskID id) const;
    std::vector<TaskPtr> getTasksByStates(const std::set<TaskState>& states) const;
    int getTotalTaskCount() const;
    TaskStats getStats() const { return m_stats; }

    TaskPtr createTask(const TaskCreateInfo& startInfo);
    TaskPtr takeTaskToRun(const std::vector<std::string>& haveResources);
    void heartbeatTask(TaskPtr task);
    void markTaskFinished(TaskPtr task); // this should be called whenever a running task finishes, whether or not it was canceled while it was running
    void markTaskShouldCancel(TaskPtr task);

    void cleanupZombieTasks(std::time_t heartbeatTimeoutSeconds);

private:
    friend class Task;

    TaskID getUnusedTaskID() const;
    bool cleanupIfZombieTask(TaskPtr task, std::time_t heartbeatTimeoutSeconds);

    std::map<PooledString, std::set<TaskID>> m_readyTasksPerRequiredResource;
    std::set<TaskID> m_readyTasksWithNoRequirements;

    TasksByID m_allTasks;
    TaskStats m_stats;
};

