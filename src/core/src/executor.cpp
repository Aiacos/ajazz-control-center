// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file executor.cpp
 * @brief Default in-process executors for @ref ajazz::core::Executor.
 */
#include "ajazz/core/executor.hpp"

#include <thread>
#include <utility>

namespace ajazz::core {

void BlockingExecutor::scheduleAfter(std::chrono::milliseconds delay,
                                     std::function<void()> task) noexcept {
    if (delay.count() > 0) {
        std::this_thread::sleep_for(delay);
    }
    if (task) {
        task();
    }
}

std::shared_ptr<Executor> defaultExecutor() {
    static auto const instance = std::make_shared<BlockingExecutor>();
    return instance;
}

} // namespace ajazz::core
