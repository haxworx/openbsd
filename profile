
PATH=$HOME/bin:/bin:/sbin:/usr/bin:/usr/sbin:/usr/X11R6/bin:/usr/local/bin:/usr/local/sbin:/usr/games:.
export PATH HOME TERM

# modify this
PROXY_SERVER="http://10.1.1.1:3128"



export http_proxy=$PROXY_SERVER
PS1="\u@\h [\w] $ "
