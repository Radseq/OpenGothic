# Windows 11 quick start

Run these scripts from `cmd.exe`, PowerShell, Windows Terminal, or by double-clicking.
They automatically switch to the repository root.

Windows uses `windows\mmo_env.cmd` for shared MMO defaults. By default it connects
to the Linux desktop MySQL server over ZeroTier:

```text
mysql://gothic:gothic_dev_password@192.168.195.94:3306/gothic_mmo_ch1_clean
```

Override `GOTHIC_MMO_MYSQL_URL` or `MYSQL_URL` before running a script if needed.

## 1. Reset clean MySQL DB

If `runtime\g2notr_ch1_pre_xardas.sqlite` does not exist yet, create it first:

```bat
windows\capture_pre_xardas_sqlite.cmd
```

Then reset the clean MySQL DB:

```bat
windows\reset_clean_mysql_from_pre_xardas.cmd
```

This drops and recreates the database named in `MYSQL_URL`.

## 2. Generate Visual Studio solutions

```bat
windows\configure_vs2022_client.cmd
windows\configure_vs2022_server.cmd
```

Solutions created:

- `build\Gothic2Notr.sln`
- `build\mmo_cpp_server\OpenGothicMmoServerCpp.sln`

Open these `.sln` files in Visual Studio 2022 if you want to build from the IDE.

## 3. Build from command line with Visual Studio generator

```bat
windows\build_vs2022_client.cmd
windows\build_vs2022_server.cmd
```

Or build both:

```bat
windows\build_vs2022_all.cmd
```

## 4. Run

Terminal 1:

```bat
windows\run_mmo_server.cmd
```

Terminal 2:

```bat
windows\run_mmo_client.cmd
```

If Gothic II is installed elsewhere, edit `GOTHIC2_DIR` in `run_mmo_client.cmd`.
