#include "cleanup.hpp"

namespace cleanup
{
    void flush(queue_type& queue)
    {
        while (!queue.empty())
        {
            queue.top()();
            queue.pop();
        }
    }
}
