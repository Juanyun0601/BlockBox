// BlockBox glue for the MultiMC net layer (not part of upstream MultiMC).
// Provides the USER_AGENT constant the same way MultiMC's BuildConfig does.

#pragma once

namespace BuildConfig
{
inline constexpr char USER_AGENT[] = "BlockBox/1.0";
}
