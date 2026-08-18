#include <rtsyn/api.hpp>
#include <rtsyn/spsc/defaults.h>

#include <cstdlib>
#include <cstring>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

namespace {

volatile sig_atomic_t stop_requested = 0;

void
handle_signal(int)
{
    stop_requested = 1;
}

std::string
env_or(const char *name, const char *fallback)
{
    const char *value = std::getenv(name);
    return value && value[0] != '\0' ? value : fallback;
}

int
env_port()
{
    const char *value = std::getenv(RTSYN_API_ENV_PORT);
    if (!value)
    {
        return RTSYN_API_DEFAULT_PORT;
    }
    return std::atoi(value);
}

bool
env_create_queues()
{
    const char *value = std::getenv(RTSYN_API_ENV_CREATE_QUEUES);
    return value && (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0);
}

} // namespace

int
main()
{
    const std::string command_name =
        env_or(RTSYN_SPSC_ENV_COMMAND_QUEUE, RTSYN_SPSC_DEFAULT_COMMAND_QUEUE);
    const std::string result_name =
        env_or(RTSYN_SPSC_ENV_RESULT_QUEUE, RTSYN_SPSC_DEFAULT_RESULT_QUEUE);
    const std::string telemetry_name =
        env_or(RTSYN_SPSC_ENV_TELEMETRY_QUEUE, RTSYN_SPSC_DEFAULT_TELEMETRY_QUEUE);
    const std::string values_name = env_or(RTSYN_SPSC_ENV_TELEMETRY_VALUES_QUEUE,
                                           RTSYN_SPSC_DEFAULT_TELEMETRY_VALUES_QUEUE);
    const bool create_queues = env_create_queues();

    rtsyn_spsc_command_shared_t command_shared = {};
    rtsyn_spsc_result_shared_t result_shared = {};
    rtsyn_spsc_telemetry_shared_t telemetry_shared = {};
    rtsyn_spsc_telemetry_values_shared_t values_shared = {};

    const int command_ok = create_queues
                               ? rtsyn_spsc_command_shared_create(&command_shared,
                                                                  command_name.c_str())
                               : rtsyn_spsc_command_shared_open(&command_shared,
                                                                command_name.c_str());
    const int result_ok =
        create_queues ? rtsyn_spsc_result_shared_create(&result_shared, result_name.c_str())
                      : rtsyn_spsc_result_shared_open(&result_shared, result_name.c_str());
    const int telemetry_ok = create_queues
                                 ? rtsyn_spsc_telemetry_shared_create(&telemetry_shared,
                                                                      telemetry_name.c_str())
                                 : rtsyn_spsc_telemetry_shared_open(&telemetry_shared,
                                                                    telemetry_name.c_str());
    const int values_ok = create_queues
                              ? rtsyn_spsc_telemetry_values_shared_create(&values_shared,
                                                                         values_name.c_str())
                              : rtsyn_spsc_telemetry_values_shared_open(&values_shared,
                                                                       values_name.c_str());

    if (command_ok != 0 || result_ok != 0 || telemetry_ok != 0 || values_ok != 0)
    {
        std::cerr << "failed to open RTSyn SPSC shared memory" << std::endl;
        rtsyn_spsc_command_shared_close(&command_shared);
        rtsyn_spsc_result_shared_close(&result_shared);
        rtsyn_spsc_telemetry_shared_close(&telemetry_shared);
        rtsyn_spsc_telemetry_values_shared_close(&values_shared);
        return EXIT_FAILURE;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
#ifdef SIGHUP
    std::signal(SIGHUP, SIG_IGN);
#endif

    rtsyn::api::Config config;
    config.command_queue = command_shared.queue;
    config.result_queue = result_shared.queue;
    config.telemetry_queue = telemetry_shared.queue;
    config.telemetry_values = values_shared.values;
    config.bind_host = env_or(RTSYN_API_ENV_HOST, RTSYN_API_DEFAULT_HOST);
    config.port = env_port();
    config.values_path = env_or(RTSYN_API_ENV_VALUES_FILE, RTSYN_API_DEFAULT_VALUES_FILE);

    rtsyn::api::Api api(config);
    if (!api.start())
    {
        std::cerr << "failed to start RTSyn API" << std::endl;
        rtsyn_spsc_command_shared_close(&command_shared);
        rtsyn_spsc_result_shared_close(&result_shared);
        rtsyn_spsc_telemetry_shared_close(&telemetry_shared);
        rtsyn_spsc_telemetry_values_shared_close(&values_shared);
        return EXIT_FAILURE;
    }

    while (!stop_requested)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    api.stop();
    rtsyn_spsc_command_shared_close(&command_shared);
    rtsyn_spsc_result_shared_close(&result_shared);
    rtsyn_spsc_telemetry_shared_close(&telemetry_shared);
    rtsyn_spsc_telemetry_values_shared_close(&values_shared);
    return EXIT_SUCCESS;
}
