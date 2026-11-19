#!/usr/bin/env bash
set -uo pipefail
# Read-only diagnostics. Never installs packages, changes drivers, starts a
# service or modifies the user's CUDA environment.
printf 'Helios Linux diagnostics\n'
uname -sm
if [[ -r /etc/os-release ]]; then
  sed -n '/^PRETTY_NAME=/p' /etc/os-release
fi
printf '\nDesktop session\n'
printf 'XDG_SESSION_TYPE=%s\n' "${XDG_SESSION_TYPE:-unavailable}"
printf 'XDG_CURRENT_DESKTOP=%s\n' "${XDG_CURRENT_DESKTOP:-unavailable}"
printf '\nBuild tools\n'
for helios_tool in cmake c++ qmake6 qtpaths6 qsb; do
  if command -v "$helios_tool" >/dev/null 2>&1; then
    case "$helios_tool" in
      qmake6) "$helios_tool" -query QT_VERSION ;;
      qtpaths6) "$helios_tool" --qt-version ;;
      *) "$helios_tool" --version 2>&1 | head -2 ;;
    esac
  else
    printf '%s: not on PATH (Qt tools may be in /usr/lib/qt6/bin)\n' "$helios_tool"
  fi
done
printf '\nNVIDIA (optional)\n'
if command -v nvidia-smi >/dev/null 2>&1; then
  nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader
else
  printf 'nvidia-smi unavailable; this does not prevent an AMD or CPU configuration.\n'
fi
printf '\nVulkan\n'
if command -v vulkaninfo >/dev/null 2>&1; then
  if command -v timeout >/dev/null 2>&1; then
    timeout 20s vulkaninfo --summary 2>&1
  else
    printf 'Run vulkaninfo --summary manually (no timeout utility installed).\n'
  fi
else
  printf 'vulkaninfo unavailable; install the distribution Vulkan diagnostic tools.\n'
fi
printf '\nThese observations do not certify rendering or CUDA compatibility.\n'
printf 'Next: make test-desktop; launch Helios in the graphical session.\n'
