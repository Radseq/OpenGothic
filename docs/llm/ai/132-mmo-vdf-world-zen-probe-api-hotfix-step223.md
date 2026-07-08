# Step223 - VDF world ZEN probe API hotfix

Step222 używał `zenkit::Vfs::getKnownFiles()`, ale lokalna wersja ZenKit w
repo nie wystawia tej metody publicznie. Build zatrzymywał się na:

```text
error: 'const class zenkit::Vfs' has no member named 'getKnownFiles'
```

Poprawka zmienia probe na stabilniejszy tryb:

- montuje VDF/MOD tak jak wcześniej;
- nie próbuje listować całego VFS;
- sprawdza publicznym `vfs.find(...)` znane nazwy świata:
  `newworld.zen`, `oldworld.zen`, `addonworld.zen`, `world.zen` oraz warianty
  `worlds/...`, `data/worlds/...`, `_work/data/worlds/...`;
- wyciąga pierwszy autorytatywny world ZEN, jeśli zostanie znaleziony.

Komendy po hotfixie zostają takie same:

```bash
cmake --build build/mmo_cpp_server --target mmo_vdf_world_zen_probe -j

tools/probe_gothic_world_zen_archives.py \
  --gothic-root "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  --world-name newworld.zen \
  --extract
```
