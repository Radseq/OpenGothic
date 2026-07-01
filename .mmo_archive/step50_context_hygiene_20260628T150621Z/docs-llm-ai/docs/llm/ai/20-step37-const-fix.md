# 20 Step37 const-correctness build fix

Fix for Step37 C++ producer build error:

```text
mmosemantichooks.cpp: passing const Npc as this argument discards qualifiers
```

Cause:
- `shouldCaptureScriptAction` accepted `const Npc*`.
- It forwarded to `shouldCapturePlayerAction(const Npc*)`.
- That helper called `Npc::world()`, but `Npc::world()` is a non-const engine API returning `World&`.

Fix:
- Change the script-capture predicate path to accept `Npc*`, not `const Npc*`.
- `GameScript::captureMmoScriptSnapshot(...)` already passes `Npc* actor`, so no caller const-cast is needed.
- This keeps the hook const-correct and avoids weakening the engine API with an artificial const overload.

Build validation:

```bash
cmake --build build -j"$(nproc)"
```

No clean rebuild should be required; the changed `mmosemantichooks.*` files are enough to trigger incremental recompilation.
