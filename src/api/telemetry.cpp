#include "rtsyn/internal/api/telemetry.hpp"

#include "rtsyn/internal/api/serialization.hpp"

#include <fstream>

namespace rtsyn::api::internal {

bool
append_values_event(const std::string &path, rtsyn_spsc_telemetry_values_t *values,
                    const rtsyn_spsc_telemetry_message_t &event, std::size_t *value_count)
{
    if (value_count)
    {
        *value_count = 0;
    }

    if (!values || event.type != RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_VALUES_WRITTEN)
    {
        return false;
    }

    const auto count = static_cast<std::size_t>(event.data.values_written.value_count);
    if (count == 0)
    {
        return true;
    }

    std::vector<rtsyn_spsc_telemetry_value_t> copied;
    copied.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        rtsyn_spsc_telemetry_value_t value = {};
        if (!rtsyn_spsc_telemetry_values_try_get(
                values, event.data.values_written.values_start_index + i, &value))
        {
            return false;
        }
        copied.push_back(value);
    }

    std::ofstream stream(path, std::ios::app);
    if (!stream)
    {
        return false;
    }

    for (const auto &value : copied)
    {
        stream << telemetry_value_to_json(event, value) << '\n';
    }

    if (!stream)
    {
        return false;
    }

    if (!rtsyn_spsc_telemetry_values_release(values, count))
    {
        return false;
    }

    if (value_count)
    {
        *value_count = count;
    }

    return true;
}

TelemetryDrain
drain_telemetry(rtsyn_spsc_telemetry_queue_t *queue, rtsyn_spsc_telemetry_values_t *values,
                const std::string &values_path, std::size_t budget)
{
    TelemetryDrain drain = {};
    if (!queue || budget == 0)
    {
        return drain;
    }

    for (std::size_t i = 0; i < budget; ++i)
    {
        rtsyn_spsc_telemetry_message_t event = {};
        if (!rtsyn_spsc_telemetry_try_pop(queue, &event))
        {
            break;
        }

        drain.result.event_count++;
        if (event.type == RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_VALUES_WRITTEN)
        {
            std::size_t copied_count = 0;
            if (append_values_event(values_path, values, event, &copied_count))
            {
                drain.result.value_count += copied_count;
            }
            else
            {
                drain.result.failed_value_event_count++;
            }
        }

        drain.events.push_back(event);
    }

    return drain;
}

} // namespace rtsyn::api::internal
