#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace MARAS::State {

    /**
     * CowTable<T> — Copy-on-Write structural sharing wrapper for a normalized WorldState table.
     *
     * Concurrency & Lifetime Contract:
     * - Single-writer (`WorldState` owner): Calls `Read()`, `Write()`, `Reset()`, and `Share()`.
     * - Multi-reader (`WorldStateSnapshot` consumers): Hold immutable `std::shared_ptr<const T>`
     *   returned by `Share()` and read lock-free with zero mutation capability.
     * - When `Write()` is called while external snapshots hold references (`use_count() > 1`),
     *   only this table `T` is deep-cloned (`std::make_shared<T>(*ptr_)`); all other tables in
     *   `WorldState` remain zero-copy shared.
     * - When the last external snapshot releases its `shared_ptr` (`fetch_sub(acq_rel)`) and
     *   `use_count()` returns to `1`, an explicit `atomic_thread_fence(memory_order_acquire)`
     *   establishes a strict C++20/23 happens-before edge prior to reusing `*ptr_` in place.
     */
    template <typename T>
    class CowTable {
    public:
        CowTable() : ptr_(std::make_shared<T>()), exported_(false) {}

        explicit CowTable(T initial)
            : ptr_(std::make_shared<T>(std::move(initial))), exported_(false) {}

        CowTable(const CowTable& other) noexcept
            : ptr_(other.ptr_), exported_(true) {
            other.exported_ = true;
        }

        CowTable(CowTable&& other) noexcept
            : ptr_(std::move(other.ptr_)), exported_(other.exported_) {
            other.ptr_ = std::make_shared<T>();
            other.exported_ = false;
        }

        CowTable& operator=(const CowTable& other) noexcept {
            if (this != &other) {
                other.exported_ = true;
                ptr_ = other.ptr_;
                exported_ = true;
            }
            return *this;
        }

        CowTable& operator=(CowTable&& other) noexcept {
            if (this != &other) {
                ptr_ = std::move(other.ptr_);
                exported_ = other.exported_;
                other.ptr_ = std::make_shared<T>();
                other.exported_ = false;
            }
            return *this;
        }

        ~CowTable() = default;

        // Read-only access (never clones)
        [[nodiscard]] const T& Read() const noexcept { return *ptr_; }
        [[nodiscard]] const T* operator->() const noexcept { return ptr_.get(); }
        [[nodiscard]] const T& operator*() const noexcept { return *ptr_; }

        // Mutable access (clones table only if shared with another CowTable or WorldStateSnapshot)
        [[nodiscard]] T& Write() {
            if (exported_) {
                if (ptr_.use_count() > 1) {
                    ptr_ = std::make_shared<T>(*ptr_);
                } else {
                    std::atomic_thread_fence(std::memory_order_acquire);
                }
                exported_ = false;
            }
            return *ptr_;
        }

        // Replaces the entire table without copying the previous contents
        void Reset(T replacement = T{}) {
            ptr_ = std::make_shared<T>(std::move(replacement));
            exported_ = false;
        }

        // Exports an immutable shared handle for lock-free snapshot readers ($O(1)$, zero copy)
        [[nodiscard]] std::shared_ptr<const T> Share() const noexcept {
            exported_ = true;
            return ptr_;
        }

        // Diagnostic/test helper: checks pointer-level structural sharing identity
        [[nodiscard]] bool SharesAllocationWith(const std::shared_ptr<const T>& snapshotHandle) const noexcept {
            return ptr_.get() == snapshotHandle.get();
        }

        [[nodiscard]] bool SharesAllocationWith(const CowTable<T>& other) const noexcept {
            return ptr_.get() == other.ptr_.get();
        }

        [[nodiscard]] long ReferenceCount() const noexcept {
            return ptr_.use_count();
        }

        [[nodiscard]] bool operator==(const CowTable& other) const {
            return ptr_.get() == other.ptr_.get() || *ptr_ == *other.ptr_;
        }

    private:
        std::shared_ptr<T> ptr_;
        mutable bool exported_{ false };
    };

}  // namespace MARAS::State
