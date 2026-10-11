#pragma once
// UART Probe-Runner feature switch (0=excluded, 1=enabled).
// Change to 1 and rebuild to restore the original runner.
#ifndef UART_PROBE_RUNNER_ENABLED
#define UART_PROBE_RUNNER_ENABLED 0
#endif

// Set to 1 and rebuild to restore battery/ADC and deep sleep.
#ifndef UART_BATTERY_ENABLED
#define UART_BATTERY_ENABLED 0
#endif
#ifndef UART_DEEP_SLEEP_ENABLED
#define UART_DEEP_SLEEP_ENABLED 0
#endif
