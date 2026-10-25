# SPDX-License-Identifier: GPL-3.0-or-later
# Source after the user's Bash configuration. DOME_PROMPT=0 opts out.
[[ $- == *i* && ${TERM:-dumb} != dumb && ${DOME_PROMPT:-1} != 0 ]] || return 0
case ${XDG_CURRENT_DESKTOP-}:${TERM_PROGRAM-} in
    *Dome*:*|*:FLTerm-XR) ;;
    *) return 0 ;;
esac
# Respect an explicitly chosen Starship profile.
[[ -z ${STARSHIP_CONFIG-} ]] || return 0
_dome_prompt_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if command -v starship >/dev/null 2>&1; then
    export STARSHIP_CONFIG="$_dome_prompt_dir/starship.toml"
    if ! declare -F starship_precmd >/dev/null; then
        eval "$(starship init bash)"
    fi
else
    # ASCII-only fallback fits every DomeD bitmap strike, with readline-safe SGR.
    PS1='\[\e[38;2;183;189;248m\][\u \w]\[\e[0m\]\n\[\e[38;2;139;213;202m\]\$ \[\e[0m\]'
fi
unset _dome_prompt_dir
