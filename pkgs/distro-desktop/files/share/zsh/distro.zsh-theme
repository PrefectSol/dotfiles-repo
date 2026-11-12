ZSH_THEME_GIT_PROMPT_PREFIX=" %{$fg[cyan]%}[%{$fg_bold[white]%}git://"
ZSH_THEME_GIT_PROMPT_SUFFIX="%{$reset_color%}%{$fg[cyan]%}]%{$reset_color%}"
ZSH_THEME_GIT_PROMPT_DIRTY=""
ZSH_THEME_GIT_PROMPT_CLEAN=""

PROMPT='%{$fg[cyan]%}[%{$fg_bold[white]%}%n@%m%{$reset_color%}%{$fg[cyan]%}]%{$reset_color%} %{$fg[cyan]%}(%{$fg_bold[white]%}%~%{$reset_color%}%{$fg[cyan]%})%{$reset_color%}$(git_prompt_info) %(?.%{$fg[cyan]%}.%{$fg[red]%})>%{$reset_color%} '
