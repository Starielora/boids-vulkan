#pragma once

#include <stack>
#include <functional>

namespace cleanup
{
    using queue_type = std::stack<std::function<void()>>;

    void flush(queue_type& queue);
}
