# CareOS zsh profile

[[ -f /usr/share/zsh/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh ]] && \
    source /usr/share/zsh/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh
[[ -f /usr/share/zsh/plugins/zsh-autosuggestions/zsh-autosuggestions.zsh ]] && \
    source /usr/share/zsh/plugins/zsh-autosuggestions/zsh-autosuggestions.zsh
[[ -f /usr/share/zsh/plugins/zsh-completions/zsh-completions.plugin.zsh ]] && \
    source /usr/share/zsh/plugins/zsh-completions/zsh-completions.plugin.zsh

autoload -U colors && colors
setopt PROMPT_SUBST AUTO_CD GLOB_DOTS NO_BEEP SHARE_HISTORY HIST_IGNORE_DUPS HIST_IGNORE_SPACE

HISTFILE=~/.zsh_history
HISTSIZE=10000
SAVEHIST=10000

CARE_BLUE=$'\033[38;2;85;154;255m'
CARE_ACCENT=$'\033[38;2;130;188;255m'
CARE_DIM=$'\033[38;2;138;153;186m'
CARE_RESET=$'\033[0m'

_careos_git_branch() {
    local branch
    branch=$(git symbolic-ref --short HEAD 2>/dev/null) || return
    echo -n " %F{#82bcff}git:$branch%f"
}

PROMPT='${CARE_BLUE}careos${CARE_RESET} ${CARE_DIM}%~${CARE_RESET}$(_careos_git_branch)
${CARE_ACCENT}>${CARE_RESET} '
RPROMPT='${CARE_DIM}%D{%H:%M}${CARE_RESET}'

autoload -Uz compinit && compinit
zstyle ':completion:*' menu select
zstyle ':completion:*' matcher-list 'm:{a-zA-Z}={A-Za-z}'
[[ -n "$LS_COLORS" ]] && zstyle ':completion:*' list-colors "${(s.:.)LS_COLORS}"

ZSH_AUTOSUGGEST_HIGHLIGHT_STYLE='fg=#3a4a6a'
typeset -A ZSH_HIGHLIGHT_STYLES
ZSH_HIGHLIGHT_STYLES[command]='fg=#559aff,bold'
ZSH_HIGHLIGHT_STYLES[builtin]='fg=#82bcff,bold'
ZSH_HIGHLIGHT_STYLES[alias]='fg=#82bcff'
ZSH_HIGHLIGHT_STYLES[string]='fg=#2ecc8e'
ZSH_HIGHLIGHT_STYLES[unknown-token]='fg=#f56060'

alias ls='ls --color=auto'
alias ll='ls -lah --color=auto'
alias la='ls -A --color=auto'
alias grep='grep --color=auto'
alias diff='diff --color=auto'
alias ip='ip --color=auto'
alias ..='cd ..'
alias ...='cd ../..'

alias care='carectl'
alias sys='carectl status'
alias doctor='carectl doctor'
alias pkg='carepkg'
alias update='careos-update'
alias info='careos-info'
alias help='careos-help'
alias install-careos='careos-install'

bindkey -e
bindkey '^[[A' history-search-backward
bindkey '^[[B' history-search-forward
bindkey '^[[1;5C' forward-word
bindkey '^[[1;5D' backward-word
bindkey '^H' backward-delete-word

if [[ $- == *i* ]] && [[ -z "$CAREOS_FETCHED" ]] && command -v fastfetch >/dev/null 2>&1; then
    export CAREOS_FETCHED=1
    fastfetch
fi
