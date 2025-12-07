#pragma once
#include <Arduino.h>
#include "NetworkTask/NetworkTask.h"

// Mức độ ưu tiên cho tác vụ mạng
enum TaskPriority : uint8_t
{
    TASK_PRIORITY_LOW      = 0, // có thể bỏ qua (telemetry thường xuyên)
    TASK_PRIORITY_NORMAL   = 1,
    TASK_PRIORITY_HIGH     = 2,
    TASK_PRIORITY_CRITICAL = 3  // không được bỏ, ví dụ: alert, validate trip
};

// Một phần tử trong hàng đợi
struct ScheduledTask
{
    NetworkTask *task = nullptr;
    TaskPriority priority = TASK_PRIORITY_LOW;
};

class NetworkInterfaceScheduler
{
public:
    static const uint8_t MAX_TASKS = 20; // tuỳ bạn chỉnh

    NetworkInterfaceScheduler()
        : _size(0) {}

    // -------------------------------------------------
    // Enqueue với priority
    //
    // Yêu cầu của bạn:
    //  - Nếu hàng đợi đang rỗng -> add luôn.
    //  - Nếu không rỗng -> tìm task có priority thấp hơn,
    //    chèn task mới phía trước, đẩy task đó xuống.
    //
    // Nếu queue FULL:
    //  - Nếu prio mới <= priority thấp nhất -> drop task mới.
    //  - Nếu prio mới > priority thấp nhất -> evict task thấp nhất,
    //    rồi chèn task mới đúng vị trí.
    // -------------------------------------------------
    bool enqueue(NetworkTask *task, TaskPriority prio)
    {
        if (!task)
            return false;

        // ===== Case 1: queue rỗng -> add luôn ở index 0 =====
        if (_size == 0)
        {
            _queue[0].task     = task;
            _queue[0].priority = prio;
            _size = 1;

            //Serial.print(F("[SCHED] Enqueued first task, prio="));
            //Serial.println((int)prio);
            debugPrintQueue();
            return true;
        }

        // ===== Case 2: còn chỗ (size < MAX_TASKS) -> chèn theo priority =====
        if (_size < MAX_TASKS)
        {
            insertByPriority(task, prio);
            //Serial.print(F("[SCHED] Enqueued task, prio="));
            //Serial.println((int)prio);
            debugPrintQueue();
            return true;
        }

        // ===== Case 3: queue FULL -> xét eviction =====
        // Vì mảng luôn sorted (ưu tiên cao ở đầu), phần tử cuối là priority thấp nhất
        uint8_t      lowestIdx  = _size - 1;
        TaskPriority lowestPrio = _queue[lowestIdx].priority;

        if (prio <= lowestPrio)
        {
            // task mới không quan trọng hơn -> bỏ luôn
            //Serial.print(F("[SCHED] Queue full, new prio="));
            //Serial.print((int)prio);
            //Serial.print(F(" <= lowest="));
            //Serial.print((int)lowestPrio);
            //Serial.println(F(" -> drop new task"));
            delete task;
            return false;
        }

        // task mới quan trọng hơn -> xoá task priority thấp nhất, chèn task mới
        //Serial.print(F("[SCHED] Queue full, evicting task with prio="));
        //Serial.println((int)lowestPrio);

        if (_queue[lowestIdx].task)
        {
            delete _queue[lowestIdx].task;
            _queue[lowestIdx].task = nullptr;
        }

        // Giảm _size vì ta coi slot cuối cùng đã được giải phóng
        _size--;

        // Bây giờ chèn task mới như bình thường vào mảng [0.._size-1]
        insertByPriority(task, prio);

        //Serial.print(F("[SCHED] Enqueued (with eviction), prio="));
        //Serial.println((int)prio);
        debugPrintQueue();
        return true;
    }

    // Non-mandatory: chỉ enqueue nếu còn chỗ, KHÔNG đẩy task khác ra
    bool enqueueIfSpace(NetworkTask *task, TaskPriority prio)
    {
        if (!task)
            return false;

        if (_size >= MAX_TASKS)
        {
            //Serial.println(F("[SCHED] Queue full, skipped non-mandatory task"));
            delete task; // tránh leak
            return false;
        }

        return enqueue(task, prio);
    }

    // Có task đang chờ không?
    bool hasPending() const
    {
        return _size > 0;
    }

    // Kiểm tra xem có task với priority >= minPrio hay không
    bool hasPendingAtLeast(TaskPriority minPrio) const
    {
        for (uint8_t i = 0; i < _size; ++i)
        {
            if (_queue[i].priority >= minPrio)
                return true;
        }
        return false;
    }

    // -------------------------------------------------
    // step(): gọi trong loop()
    //
    // - Không còn "run một lần rồi xoá".
    // - Bây giờ mỗi task là non-blocking → mỗi vòng chỉ gọi execute()
    //   cho task ở đầu hàng đợi.
    // - Nếu task báo completed (task->isCompleted() == true),
    //   ta xoá nó và dồn các task phía sau lên.
    // -------------------------------------------------
    void step()
    {
        if (_size == 0)
            return;

        ScheduledTask &current = _queue[0];

        if (!current.task)
        {
            // Nếu vì lý do gì đó task null -> bỏ nó
            shiftLeftFromIndex(0);
            return;
        }

        // Gọi execute (non-blocking, phải được task tự đảm bảo)
        current.task->execute();

        // Nếu task đã xong -> xoá
        if (current.task->isCompleted())
        {
            //Serial.print(F("[SCHED] Task completed, prio="));
            //Serial.println((int)current.priority);

            delete current.task;
            current.task = nullptr;

            shiftLeftFromIndex(0);
            debugPrintQueue();
        }
    }

    // Kích thước hiện tại
    uint8_t size() const { return _size; }

private:
    ScheduledTask _queue[MAX_TASKS];
    uint8_t _size;

    // -------------------------------------------------
    // Chèn task theo priority, giả định _size < MAX_TASKS
    // và mảng[0.._size-1] đã sorted (priority giảm dần).
    //
    // "Push task thấp hơn xuống" = đúng theo yêu cầu:
    //   - tìm phần tử đầu tiên có priority < prio mới
    //   - chèn task mới trước nó, dồn nó xuống.
    // -------------------------------------------------
    void insertByPriority(NetworkTask *task, TaskPriority prio)
    {
        int insertPos = _size; // mặc định chèn cuối (nếu không tìm thấy priority thấp hơn)

        for (int i = 0; i < _size; ++i)
        {
            if (prio > _queue[i].priority)
            {
                insertPos = i;
                break;
            }
        }

        // dồn phần tử phía sau để chừa chỗ
        for (int j = _size; j > insertPos; --j)
        {
            _queue[j] = _queue[j - 1];
        }

        _queue[insertPos].task     = task;
        _queue[insertPos].priority = prio;
        _size++;
    }

    // Dồn phần tử từ index+1 lên trên
    void shiftLeftFromIndex(uint8_t index)
    {
        if (index >= _size)
            return;

        for (uint8_t i = index + 1; i < _size; ++i)
        {
            _queue[i - 1] = _queue[i];
        }
        _size--;
    }

    void debugPrintQueue()
    {
        //Serial.print(F("[SCHED] Queue size="));
        //Serial.print(_size);
        //Serial.print(F(" ["));
        for (uint8_t i = 0; i < _size; ++i)
        {
            Serial.print((int)_queue[i].priority);
            if (i + 1 < _size)
                Serial.print(',');
        }
        //Serial.println(']');
    }
};