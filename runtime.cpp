// Runtime entry point for the language-level sync() expression.
// This is a stub for now: once spawn exists, this function will block until
// the runtime's outstanding task count reaches zero.
extern "C" double __compiler_sync_tasks() { return 0.0; }
