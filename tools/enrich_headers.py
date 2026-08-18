#!/usr/bin/env python3
"""
Header Documentation Enricher for Corium.
Standardizes Doxygen header comments across all headers in include/corium.
"""

from pathlib import Path
import re

INCLUDE_DIR = Path(__file__).resolve().parent.parent / "include" / "corium"

MODULE_MAP = {
    "async": ("async", "C++20 Coroutine Task and Asynchronous Primitives"),
    "embedded": ("embedded", "Embedded & Hardware ISR Primitives"),
    "fsm": ("fsm", "Compile-Time Finite State Machine"),
    "internal": ("core", "Internal Fast Delegate, Ring Buffer & Queue Engine"),
    "ipc": ("ipc", "Inter-Process Communication Channels & Shared Memory"),
    "logging": ("logging", "Zero-Heap Logging Framework"),
    "policies": ("policies", "Compile-Time Strategy Policies"),
    "profiler": ("profiler", "Real-Time Telemetry & Flight Recorder"),
    "safety": ("safety", "Safety, Watchdogs & Circuit Breaker"),
    "timers": ("timers", "Timers & Hardware Clocks"),
    "wire": ("wire", "Binary Wire Framing & Serialization Protocol"),
    "": ("core", "Core MPSC Application Engine & Runtime")
}

DESCRIPTIONS = {
    "Application.hpp": "CRTP static polymorphism application base class with auto-deduced event handlers.",
    "ApplicationContext.hpp": "Type-erased context for application runtime introspection and lifecycle control.",
    "BackgroundService.hpp": "Managed worker thread using C++20 std::jthread and std::stop_token.",
    "EventBus.hpp": "Multi-producer single-consumer lock-free event bus coordinator.",
    "EventSink.hpp": "Non-allocating type-erased fat pointer handle for lock-free event posting.",
    "Events.hpp": "Standard lifecycle events (QuitEvent, ErrorEvent, TimerEvent).",
    "Runtime.hpp": "Deterministic single-consumer event loop coordinator and runner.",
    "RuntimeBuilder.hpp": "Fluent compile-time builder for custom policy-configured runtimes.",
    "Service.hpp": "Lightweight thread-safe background service interface.",
    "ServiceContext.hpp": "Dependency injection context for background services.",
    "ServiceRegistry.hpp": "Fixed-capacity static container for background worker services.",
    "corium.hpp": "Master umbrella header for the entire Corium runtime framework.",
    
    # async
    "CancellationToken.hpp": "Lock-free atomic cooperative cancellation token with coroutine awaiter.",
    "Delay.hpp": "Non-blocking timer delay and yield awaitables for C++20 coroutines.",
    "Generator.hpp": "Pull-based zero-heap lazy sequence generator compatible with C++20 ranges.",
    "Task.hpp": "Lazy awaitable C++20 coroutine task with zero dynamic heap allocation.",
    "WhenAll.hpp": "Non-blocking combinator awaiting completion of multiple parallel tasks.",
    "WhenAny.hpp": "Non-blocking combinator resolving on the first completed task.",
    "async.hpp": "Umbrella header for C++20 coroutine primitives.",
    
    # embedded
    "FreeRtos.hpp": "FreeRTOS ISR event sink and hardware context-switching helpers.",
    "InterruptLock.hpp": "Zero-overhead RAII critical section masking across ARM, ESP32, and host.",
    "IsrSink.hpp": "Safe non-blocking event posting handle for hardware interrupt service routines.",
    "embedded.hpp": "Umbrella header for embedded and RTOS integration primitives.",
    
    # fsm
    "HistoryState.hpp": "Tag type for shallow history pseudostate in hierarchical state machines.",
    "StateMachine.hpp": "Zero-heap variant-based active finite state machine coordinator.",
    "Transition.hpp": "Declarative compile-time transition rules, internal transitions, and action lists.",
    "fsm.hpp": "Umbrella header for compile-time finite state machines.",
    
    # internal
    "CallableTraits.hpp": "Compile-time introspection traits for callable objects and event handlers.",
    "EventQueue.hpp": "Internal priority and bounded lock-free event queue adapter.",
    "FastDelegate.hpp": "Zero-allocating Small Buffer Optimized (SBO) static delegate dispatcher.",
    "MpscRingBuffer.hpp": "Dmitry Vyukov's lock-free multi-producer single-consumer ring buffer algorithm.",
    "Panic.hpp": "Zero-heap assertion and panic handling utilities.",
    "Reactor.hpp": "Internal static event handler registry and compile-time dispatcher.",
    "VariantIndex.hpp": "Compile-time type index resolution for std::variant alternative types.",
    
    # ipc
    "DomainSocket.hpp": "UNIX Domain Socket datagram listener and client implementation.",
    "IpcChannel.hpp": "Zero-copy typed event exchange over POSIX shared memory.",
    "PlatformChannel.hpp": "Cross-platform portable IPC channel alias.",
    "SharedMemory.hpp": "RAII wrapper for POSIX shared memory and Windows file mappings.",
    "ShmMpscQueue.hpp": "Lock-free multi-producer single-consumer queue located in shared memory.",
    "UdsChannel.hpp": "UNIX Domain Socket datagram inter-process event channel.",
    "ipc.hpp": "Umbrella header for inter-process communication primitives.",
    
    # logging
    "LogBackgroundService.hpp": "Asynchronous background worker service for flushing log events to disk.",
    "LogEvent.hpp": "Zero-heap fixed-capacity inline buffer log event record.",
    "LogLevel.hpp": "Log severity enumeration and ANSI color formatting utilities.",
    "Logger.hpp": "Static logger frontend with compile-time formatting and severity filtering.",
    "logging.hpp": "Umbrella header for the zero-heap structured logging framework.",
    "ConsoleLogSink.hpp": "Standard console output log sink with ANSI color support.",
    "FileLogSink.hpp": "Synchronous append-only file logging sink.",
    "JsonLogSink.hpp": "Structured JSON Lines (NDJSON) output log sink for observability.",
    "NullLogSink.hpp": "No-op compile-time disabled log sink for zero overhead.",
    
    # policies
    "OverflowPolicies.hpp": "Queue saturation policies (DropNewest, DropOldest, Audit, Panic).",
    "Policies.hpp": "Umbrella header for compile-time runtime strategy policies.",
    "QueuePolicies.hpp": "Bounded and multi-tier priority MPSC queueing policies.",
    "SignalPolicies.hpp": "Thread wake-up policies (NoSignalPolicy, ConditionVariableSignalPolicy).",
    "StoragePolicies.hpp": "Static storage capacity policies for FastDelegate SBO inline buffers.",
    "TimerPolicies.hpp": "Timer scheduler static capacity and storage policies.",
    
    # profiler
    "FlightRecorder.hpp": "Circular in-memory telemetry buffer with Chrome Tracing JSON export.",
    "ProfilerPolicies.hpp": "Latency tracking and flight recording policies with runtime toggle.",
    "profiler.hpp": "Umbrella header for real-time latency telemetry and flight recorder.",
    
    # safety
    "CircuitBreaker.hpp": "Lock-free circuit breaker state machine for active fault isolation.",
    "HeartbeatMonitor.hpp": "Lock-free SLA deadline tracker for multi-service heartbeats.",
    "SafetyEvents.hpp": "Heartbeat and fault notification event structures.",
    "WatchdogService.hpp": "Autonomous background service for hardware watchdog supervision.",
    "WatchdogSupervisor.hpp": "Multi-task SLA deadline monitor controlling hardware watchdog refresh.",
    "safety.hpp": "Umbrella header for safety, supervision, and fault recovery primitives.",
    
    # timers
    "ClockPolicies.hpp": "Hardware and simulated clock policies (Chrono, Manual, Tick, EspTimer, FreeRTOS).",
    "TimerScheduler.hpp": "Fixed-capacity static timer scheduler for delayed and periodic events.",
    
    # wire
    "Serializer.hpp": "Type-safe serialization and direct event sink deserialization.",
    "WirePacket.hpp": "Binary packet framing with CRC-16 checksum and schema versioning.",
    "wire.hpp": "Umbrella header for binary wire protocol framing and serialization."
}

def enrich_file(filepath: Path):
    rel_path = filepath.relative_to(INCLUDE_DIR.parent)
    subfolder = filepath.parent.name if filepath.parent != INCLUDE_DIR.parent else ""
    if subfolder == "sinks":
        subfolder = "logging"
    elif subfolder == "corium":
        subfolder = ""

    group_name, group_desc = MODULE_MAP.get(subfolder, ("core", "Core MPSC Engine"))
    filename = filepath.name
    brief = DESCRIPTIONS.get(filename, f"Corium {filename} header.")

    content = filepath.read_text(encoding="utf-8")
    
    # Strip existing @file block if present
    content_stripped = re.sub(r'/\*\*[\s\S]*?@file[\s\S]*?\*/\s*', '', content)
    content_stripped = re.sub(r'///\s*@file[^\n]*\n(///[^\n]*\n)*', '', content_stripped)
    
    # Ensure #pragma once
    if "#pragma once" in content_stripped:
        content_stripped = content_stripped.replace("#pragma once", "").lstrip()

    header_block = (
        f"/**\n"
        f" * @file {filename}\n"
        f" * @ingroup {group_name}\n"
        f" * @brief {brief}\n"
        f" */\n\n"
        f"#pragma once\n\n"
    )

    new_content = header_block + content_stripped
    filepath.write_text(new_content, encoding="utf-8")
    print(f"Enriched: {rel_path} -> group: {group_name}")

def main():
    headers = sorted(INCLUDE_DIR.glob("**/*.hpp"))
    for h in headers:
        enrich_file(h)
    print(f"\nEnriched all {len(headers)} headers successfully.")

if __name__ == "__main__":
    main()
