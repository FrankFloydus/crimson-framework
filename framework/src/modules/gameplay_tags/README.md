<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Gameplay Tags

Use a gameplay tag to name a rule, state, or event that several systems need to understand. A stunned character can carry `state.control.stun`. A dash can check for `state.control` to block every control impairment, while a stun animation can check for `state.control.stun` specifically. Games that only need a flag on one object can use a local property.

Godot exposes tags and containers to GDScript, Visual Script, and the Inspector. The native C++17 module uses the standard library and runs independently of the scene tree. See the [gameplay system proposal](../../../docs/GAMEPLAY_SYSTEM_PROPOSAL.md) for how abilities and effects will use tags.

## Choose tags in the Inspector

Open the [Inspector preview scene](../../../project/tests/gameplay_tags/inspector_preview.tscn) and select its node. Selected Tag accepts one `GameplayTag`. Owned Tags accepts a `GameplayTagContainer`. Click either field to open the picker. An empty container shows Add Tags inside the field. Its chips wrap to the next line as space runs out. Select a chip to open the picker at its path or click its **×** to remove it.

Checking `state.control.stun` also checks `state.control` and `state` in the tree. The applied selection has a chip for `state.control.stun`. Unchecking a branch clears its selected descendants and selects the direct parent if no direct sibling is selected explicitly. Use Options → Clear Selection to empty the field.

Use New Tag to declare a path, or use Options to add a child, rename a path, delete a declared leaf, or copy a name. Catalog edits save to the project configuration after confirmation. Apply Tag and Apply Tags assign a picker selection to the inspected property. Cancel discards selection edits. Each applied selection is a separate property edit, even when another field shared the original Resource.

Registering `state.control.stun` makes `state.control` available. If a tag is renamed, saved scenes and resources resolve its redirect when loaded. If a tag is deleted, a property that already holds the path can still show it. Use the chip or picker to replace or clear the assignment.

## Read tags from scripts

Use `GameplayTag.find(&"state.control")` to resolve a registered path or receive `null` when it is unavailable. `GameplayTag.from_name()` accepts any syntactically valid path. For a saved container, use `has()` to include ancestor matching and `has_exact()` to check explicit membership. When a character needs to mutate a container independently of its authored Resource, start from `copy()`.

`GameplayTagQuery` factories capture their terms when called. Use `any_tags()`, `all_tags()`, and `no_tags()` for tag conditions, and `any_queries()`, `all_queries()`, and `no_queries()` to combine them. An empty any group fails. Empty all and none groups pass. An unset query fails. Evaluate a predicate with `matches_query()` or `GameplayTagQuery.matches()`.

Visual Script exposes the same methods and typed arrays. Choose `GameplayTag` or `GameplayTagContainer` for a variable, then enable Show in Inspector on the declaration to use the same picker on the attached scene node. The result of `GameplayTagContainer.get_tags()` carries the `GameplayTag` element type into For Each.

Find tag, container, catalog, and query nodes under Gameplay Tags in the Visual Script node picker. On an unwired tag input, click its value button and choose a path through the same picker. Container and typed tag array inputs accept several paths. Wire a value to supply it at runtime.

## Block an action in C++

Declare the paths your game uses in a `GameplayTagCatalog` during setup. Registering `state.control.stun` also makes its ancestors available for lookup. Resolve the blocking tag once and reuse it when several actions run the same check.

```cpp
#include "modules/gameplay_tags/gameplay_tag_catalog.hpp"
#include "modules/gameplay_tags/gameplay_tag_container.hpp"

#include <iostream>

using crimson::gameplay_tags::GameplayTag;
using crimson::gameplay_tags::GameplayTagCatalog;
using crimson::gameplay_tags::GameplayTagContainer;

int main() {
    GameplayTagCatalog catalog;
    const GameplayTag stun = catalog.register_tag("state.control.stun");
    const GameplayTag control = catalog.find("state.control");

    GameplayTagContainer owned_tags;
    owned_tags.add(stun);
    std::cout << std::boolalpha;
    std::cout << "Dash blocked: " << owned_tags.has(control) << '\n';
    std::cout << "Control explicitly granted: " << owned_tags.has_exact(control) << '\n';

    owned_tags.remove(stun);
    std::cout << "Dash available: " << !owned_tags.has(control) << '\n';
}
```

The example prints `true`, `false`, and `true` for the three checks. Compile it in place of the regression fixture source in the command below.

Give each game its own catalog. `register_tag()` and `find()` return an invalid tag for malformed input, and `find()` also returns an invalid tag for unregistered paths. Check `is_valid()` when names come from game data. Matching with an invalid tag returns false. Tag values own their canonical names and containers own their members, so copies stay usable after their original catalog is destroyed.

Paths consist of nonempty segments of ASCII letters, digits, and underscores, separated by dots. Lookup folds ASCII case, and `name()` returns the lowercase spelling. `children()` and `descendants()` enumerate registered paths in lexical order. Use `is_declared()` or `tags(true)` to identify paths declared by the game, including an implicit ancestor that was later registered explicitly.

## Explicit and inherited membership

A container keeps unique explicit tags in insertion order. A tag matches itself and its ancestors by whole segment. Inherited membership is calculated from the explicit tags when you ask for it.

| Operation | Behavior |
| --- | --- |
| `has`, `has_any`, `has_all` | Allow a held child to satisfy a parent requirement. The requirements are the explicit tags in the supplied container. |
| `has_exact`, `has_any_exact`, `has_all_exact` | Require exact explicit membership. Both any operations return false for an empty requirement set, and both all operations return true. |
| `add`, `remove`, `append`, `remove_all` | Modify explicit membership. Adding a child with `add` preserves a separately held parent. Removing one child leaves the parent available if another child still implies it. |
| `add_leaf` | Adds a tag and removes its explicitly held ancestors. An existing exact tag succeeds. An invalid tag or a tag already implied by a held descendant returns false. |
| `filter`, `filter_exact`, `append_matching` | Preserve qualifying explicit source tags. The hierarchical variants match a source tag against an explicit requirement by exact identity or ancestry. |
| `with_parents`, `tags`, `operator==` | Parent expansion returns a new container with all ancestors explicitly present. Enumeration reads explicit tags. Equality compares explicit membership regardless of insertion order. |

## Run the native regression fixture

From `framework/` in a Visual Studio x64 Developer PowerShell, compile the [native fixture](../../../scripts/fixtures/gameplay_tags_test.cpp) in a temporary directory. The same fixture can be compiled with `/O2 /DNDEBUG` to check optimized code.

```powershell
$frameworkRoot = (Get-Location).Path
$tagTestDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tagTestDirectory | Out-Null
Push-Location -LiteralPath $tagTestDirectory
try {
    $tagSources = @(
        "$frameworkRoot/scripts/fixtures/gameplay_tags_test.cpp"
        "$frameworkRoot/src/modules/gameplay_tags/gameplay_tag.cpp"
        "$frameworkRoot/src/modules/gameplay_tags/gameplay_tag_catalog.cpp"
        "$frameworkRoot/src/modules/gameplay_tags/gameplay_tag_container.cpp"
        "$frameworkRoot/src/modules/gameplay_tags/gameplay_tag_query.cpp"
    )
    cl.exe /nologo /std:c++17 /EHsc /W4 /WX "/I$frameworkRoot/src" /Fe:gameplay_tags_test.exe $tagSources
    if ($LASTEXITCODE -eq 0) {
        .\gameplay_tags_test.exe
    }
} finally {
    Pop-Location
}
```
