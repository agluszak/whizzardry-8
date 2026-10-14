#pragma once
namespace wiz8::testing {
// Thread-local fixture instrumentation. A hook may throw before an operation;
// destructors always release streams without invoking it.
enum class FileOperation { read, write, seek, flush, close, publish };
using FileOperationHook = void (*)(FileOperation);
FileOperationHook set_file_operation_hook(FileOperationHook hook);
}
