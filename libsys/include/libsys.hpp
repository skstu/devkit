#pragma once

// Legacy umbrella: preserve transitive platform includes for existing hosts.
// New callers should include <libsys/system.h> or <libsys/console_input.h>.
#include <libsys/detail/native_headers.h>
#include <libsys/system.h>
#include <libsys/console_input.h>
#include <libsys/file_lock.h>
