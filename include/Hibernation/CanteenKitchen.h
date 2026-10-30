#pragma once
// The same persistent kitchen is used by BSimmer and the companion plugin.
// Desktop presentation, model tools and encrypted persistence stay in the host.
#include <cuisine/Kitchen.h>
#include <cuisine/RecipeRules.h>
#include <Hibernation/RolePlugin.h>
namespace hibernation::canteen {
using Json = bsimmer::cuisine::Json;
using Kitchen = bsimmer::cuisine::Kitchen;
}
