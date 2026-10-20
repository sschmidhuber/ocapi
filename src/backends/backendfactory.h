#pragma once

#include <memory>

namespace ocapi {

struct BackendConfig;
class IBackend;

// Creates a backend instance for the given configuration. The caller owns
// the returned backend.
[[nodiscard]] std::unique_ptr<IBackend> createBackend(const BackendConfig& config);

} // namespace ocapi
