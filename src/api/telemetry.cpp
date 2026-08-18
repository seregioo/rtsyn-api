#include "rtsyn/internal/api/telemetry.hpp"

#include "rtsyn/internal/api/serialization.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>

namespace rtsyn::api::internal {

namespace {

std::string
csv_escape(const std::string &value)
{
    const bool quote = value.find_first_of(",\"\n\r") != std::string::npos;
    if (!quote)
    {
        return value;
    }

    std::string escaped = "\"";
    for (const char character : value)
    {
        if (character == '"')
        {
            escaped += "\"\"";
        }
        else
        {
            escaped += character;
        }
    }
    escaped += '"';
    return escaped;
}

std::string
sample_string(const rtsyn_spsc_telemetry_value_t &value)
{
    const auto end = std::find(value.data.string,
                               value.data.string + RTSYN_SPSC_TELEMETRY_VALUE_STRING_MAX_SIZE,
                               '\0');
    return std::string(value.data.string, end);
}

std::string
value_to_csv(const rtsyn_spsc_telemetry_value_t &value)
{
    std::ostringstream out;
    switch (value.value_type)
    {
        case RTSYN_ABI_VALUE_F32:
            out << value.data.f32;
            break;
        case RTSYN_ABI_VALUE_F64:
            out << value.data.f64;
            break;
        case RTSYN_ABI_VALUE_I64:
            out << value.data.i64;
            break;
        case RTSYN_ABI_VALUE_U64:
            out << value.data.u64;
            break;
        case RTSYN_ABI_VALUE_STRING:
            out << csv_escape(sample_string(value));
            break;
        default:
            break;
    }
    return out.str();
}

bool
measurement_field_supported(const std::string &field)
{
    return field == "period_ns" || field == "actual_period_ns" || field == "latency_ns"
           || field == "wake_lateness_ns" || field == "skipped_cycle_count"
           || field == "missed_cycle" || field == "deadline_missed"
           || field == "telemetry_dropped_events"
           || field == "devices_read_ns"
           || field == "plugins_time_ns" || field == "devices_write_ns";
}

std::string
measurement_field_to_csv(const rtsyn_spsc_telemetry_message_t &event, const std::string &field)
{
    const auto &measurement = event.data.measurement;
    if (field == "period_ns")
    {
        return std::to_string(measurement.period_ns);
    }
    if (field == "actual_period_ns")
    {
        return std::to_string(measurement.actual_period_ns);
    }
    if (field == "latency_ns")
    {
        return std::to_string(measurement.latency_ns);
    }
    if (field == "wake_lateness_ns")
    {
        return std::to_string(measurement.wake_lateness_ns);
    }
    if (field == "skipped_cycle_count")
    {
        return std::to_string(measurement.skipped_cycle_count);
    }
    if (field == "missed_cycle")
    {
        return measurement.missed_cycle ? "1" : "0";
    }
    if (field == "deadline_missed")
    {
        return measurement.deadline_missed ? "1" : "0";
    }
    if (field == "telemetry_dropped_events")
    {
        return std::to_string(event.dropped_event_count);
    }
    if (field == "devices_read_ns")
    {
        return std::to_string(measurement.devices_read_ns);
    }
    if (field == "plugins_time_ns")
    {
        return std::to_string(measurement.plugins_time_ns);
    }
    if (field == "devices_write_ns")
    {
        return std::to_string(measurement.devices_write_ns);
    }
    return {};
}

bool
csv_sink_valid(const TelemetryCsvSink &sink)
{
    return sink.enabled && !sink.path.empty() && !sink.names.empty()
           && sink.names.size() == sink.values.size() + sink.measurement_fields.size()
           && std::all_of(sink.measurement_fields.begin(), sink.measurement_fields.end(),
                          measurement_field_supported);
}

bool
csv_value_matches(const TelemetryCsvValueSelector &selector,
                  const rtsyn_spsc_telemetry_value_t &value)
{
    return (selector.node_id == UINT32_MAX || selector.node_id == value.node_id)
           && selector.value_id == value.value_id
           && (selector.value_kind == RTSYN_SPSC_TELEMETRY_VALUE_KIND_NONE
               || selector.value_kind == value.value_kind);
}

bool
ensure_parent_directory(const std::string &path)
{
    std::error_code error;
    const auto parent = std::filesystem::path(path).parent_path();
    if (parent.empty() || std::filesystem::exists(parent, error))
    {
        return !error;
    }
    if (std::filesystem::create_directories(parent, error))
    {
        return true;
    }
    error.clear();
    return std::filesystem::exists(parent, error) && !error;
}

bool
write_csv_row(TelemetryCsvSink &sink)
{
    if (!sink.pending_valid)
    {
        return true;
    }

    if (!ensure_parent_directory(sink.path))
    {
        return false;
    }

    if (!sink.stream)
    {
        return false;
    }

    sink.stream << sink.pending_cycle_id << ',' << sink.pending_timestamp_ns;
    for (const auto &column : sink.pending_columns)
    {
        sink.stream << ',' << column;
    }
    sink.stream << '\n';
    return static_cast<bool>(sink.stream);
}

bool
prepare_csv_row(TelemetryCsvSink &sink, uint64_t cycle_id, uint64_t timestamp_ns)
{
    if (sink.pending_valid && sink.pending_cycle_id != cycle_id && !write_csv_row(sink))
    {
        return false;
    }

    if (!sink.pending_valid || sink.pending_cycle_id != cycle_id)
    {
        sink.pending_valid = true;
        sink.pending_cycle_id = cycle_id;
        sink.pending_timestamp_ns = timestamp_ns;
        sink.pending_columns.assign(sink.names.size(), {});
    }
    return true;
}

bool
append_csv_values(TelemetryCsvSink &sink,
                  const std::vector<rtsyn_spsc_telemetry_value_t> &values)
{
    if (!csv_sink_valid(sink) || sink.values.empty())
    {
        return true;
    }

    for (const auto &value : values)
    {
        const auto found = std::find_if(
            sink.values.begin(), sink.values.end(),
            [&value](const auto &selector) { return csv_value_matches(selector, value); });
        if (found == sink.values.end())
        {
            continue;
        }

        if (!prepare_csv_row(sink, value.cycle_id, value.timestamp_ns))
        {
            return false;
        }

        const auto column = static_cast<std::size_t>(found - sink.values.begin());
        sink.pending_columns[column] = value_to_csv(value);
    }

    return true;
}

bool
append_csv_measurement(TelemetryCsvSink &sink, const rtsyn_spsc_telemetry_message_t &event)
{
    if (!csv_sink_valid(sink) || sink.measurement_fields.empty()
        || event.type != RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_MEASUREMENT)
    {
        return true;
    }

    const auto cycle_id = event.data.measurement.cycle_id;
    if (!prepare_csv_row(sink, cycle_id, event.timestamp_ns))
    {
        return false;
    }

    const auto offset = sink.values.size();
    for (std::size_t i = 0; i < sink.measurement_fields.size(); ++i)
    {
        sink.pending_columns[offset + i] =
            measurement_field_to_csv(event, sink.measurement_fields[i]);
    }
    return true;
}

} // namespace

bool
initialize_csv_sink_file(TelemetryCsvSink &sink)
{
    if (!csv_sink_valid(sink))
    {
        return false;
    }

    if (!ensure_parent_directory(sink.path))
    {
        return false;
    }

    sink.stream.open(sink.path, std::ios::trunc);
    if (!sink.stream)
    {
        return false;
    }

    sink.stream << "cycle_id,timestamp_ns";
    for (const auto &name : sink.names)
    {
        sink.stream << ',' << csv_escape(name);
    }
    sink.stream << '\n';
    return static_cast<bool>(sink.stream);
}

bool
flush_csv_sink(TelemetryCsvSink &sink)
{
    if (!csv_sink_valid(sink))
    {
        return true;
    }

    const bool flushed = write_csv_row(sink);
    sink.stream.flush();
    sink.stream.close();
    sink.pending_valid = false;
    sink.pending_columns.clear();
    return flushed;
}

std::optional<std::vector<rtsyn_spsc_telemetry_value_t>>
append_values_event(const std::string &path, rtsyn_spsc_telemetry_values_t *values,
                    const rtsyn_spsc_telemetry_message_t &event, std::size_t *value_count,
                    TelemetryCsvSink *csv_sink, std::ofstream *values_stream)
{
    if (value_count)
    {
        *value_count = 0;
    }

    if (!values || event.type != RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_VALUES_WRITTEN)
    {
        return std::nullopt;
    }

    const auto count = static_cast<std::size_t>(event.data.values_written.value_count);
    if (count == 0)
    {
        return std::vector<rtsyn_spsc_telemetry_value_t>{};
    }

    std::vector<rtsyn_spsc_telemetry_value_t> copied;
    copied.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        rtsyn_spsc_telemetry_value_t value = {};
        if (!rtsyn_spsc_telemetry_values_try_get(
                values, event.data.values_written.values_start_index + i, &value))
        {
            return std::nullopt;
        }
        copied.push_back(value);
    }

    std::ofstream local_stream;
    std::ofstream *stream = values_stream;
    if (!stream)
    {
        local_stream.open(path, std::ios::app);
        stream = &local_stream;
    }
    else if (!*stream)
    {
        if (!ensure_parent_directory(path))
        {
            return std::nullopt;
        }
        stream->open(path, std::ios::app);
    }
    if (!*stream)
    {
        return std::nullopt;
    }

    for (const auto &value : copied)
    {
        *stream << telemetry_value_to_json(event, value) << '\n';
    }

    if (!*stream)
    {
        return std::nullopt;
    }

    if (csv_sink && !append_csv_values(*csv_sink, copied))
    {
        return std::nullopt;
    }

    if (!rtsyn_spsc_telemetry_values_release(values, count))
    {
        return std::nullopt;
    }

    if (value_count)
    {
        *value_count = count;
    }

    return copied;
}

TelemetryDrain
drain_telemetry(rtsyn_spsc_telemetry_queue_t *queue, rtsyn_spsc_telemetry_values_t *values,
                const std::string &values_path, std::size_t budget,
                TelemetryCsvSink *csv_sink, std::ofstream *values_stream)
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
            if (auto copied = append_values_event(values_path, values, event, &copied_count,
                                                  csv_sink, values_stream))
            {
                drain.result.value_count += copied_count;
                drain.values.insert(drain.values.end(), copied->begin(), copied->end());
            }
            else
            {
                drain.result.failed_value_event_count++;
            }
        }
        else if (csv_sink && event.type == RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_MEASUREMENT
                 && !append_csv_measurement(*csv_sink, event))
        {
            drain.result.failed_value_event_count++;
        }

        drain.events.push_back(event);
    }

    // Flush once per drain batch, not once per sample/event. The API can be
    // consuming tens of thousands of events per second at short engine periods.
    if (csv_sink && csv_sink->stream)
    {
        csv_sink->stream.flush();
    }
    if (values_stream && *values_stream)
    {
        values_stream->flush();
    }

    return drain;
}

} // namespace rtsyn::api::internal
