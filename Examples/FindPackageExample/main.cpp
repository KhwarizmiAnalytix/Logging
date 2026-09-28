#include "include/logger/logger.h"
#include "include/logger/config.h"

#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    // Initialize logging with default config
    logging::logger::init();

    // Log messages at different levels
    logging::logger::log(
        logging::logger_verbosity_enum::VERBOSITY_INFO,
        __FILE__,
        __LINE__,
        "find_package(Logging) example application started");

    logging::logger::log(
        logging::logger_verbosity_enum::VERBOSITY_INFO,
        __FILE__,
        __LINE__,
        "This application was linked using an installed Logging package");

    // Test scopes
    {
        logging::logger::start_scope(
            logging::logger_verbosity_enum::VERBOSITY_INFO,
            "example_scope",
            __FILE__,
            __LINE__);

        logging::logger::log(
            logging::logger_verbosity_enum::VERBOSITY_INFO,
            __FILE__,
            __LINE__,
            "Inside a named scope");

        logging::logger::end_scope("example_scope");
    }

    logging::logger::log(
        logging::logger_verbosity_enum::VERBOSITY_INFO,
        __FILE__,
        __LINE__,
        "Application completed successfully");

    return 0;
}
