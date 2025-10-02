#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>

template <typename T>
class BoundedBlockingQueue
{
public:
    explicit BoundedBlockingQueue(std::size_t capacity) : capacity_(capacity)
    {
        if (capacity_ == 0)
        {
            throw std::invalid_argument("Queue capacity must be greater than zero");
        }
    }

    bool push(const T& value)
    {
        std::unique_lock lock(mutex_);
        not_full_cv_.wait(lock, [&] { return closed_ || queue_.size() < capacity_; });
        if (closed_)
        {
            return false;
        }
        queue_.push(value);
        not_empty_cv_.notify_one();
        return true;
    }

    bool push(T&& value)
    {
        std::unique_lock lock(mutex_);
        not_full_cv_.wait(lock, [&] { return closed_ || queue_.size() < capacity_; });
        if (closed_)
        {
            return false;
        }
        queue_.push(std::move(value));
        not_empty_cv_.notify_one();
        return true;
    }

    std::optional<T> pop()
    {
        std::unique_lock lock(mutex_);
        not_empty_cv_.wait(lock, [&] { return closed_ || !queue_.empty(); });
        if (queue_.empty())
        {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop();
        not_full_cv_.notify_one();
        return value;
    }

    void close()
    {
        {
            std::scoped_lock lock(mutex_);
            closed_ = true;
        }
        not_empty_cv_.notify_all();
        not_full_cv_.notify_all();
    }

    [[nodiscard]] bool closed() const noexcept
    {
        std::scoped_lock lock(mutex_);
        return closed_;
    }

    [[nodiscard]] std::size_t size() const
    {
        std::scoped_lock lock(mutex_);
        return queue_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_cv_;
    std::condition_variable not_full_cv_;
    std::queue<T> queue_;
    bool closed_{false};
};
