d=${XDG_DATA_HOME:-$HOME/.local/share}/distro
case ":$PATH:" in
*":$d/gcc/bin:"*) ;;
*) PATH=$HOME/.local/bin:$d/gcc/bin:$d/cuda/bin:$PATH ;;
esac
if [ -L "$d/cuda" ]; then
	export CUDA_HOME=$d/cuda CUDA_PATH=$d/cuda
	case ":${LD_LIBRARY_PATH:-}:" in
	*":$d/cuda/lib64:"*) ;;
	*) export LD_LIBRARY_PATH=$d/cuda/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH} ;;
	esac
fi
if [ -e /proc/driver/nvidia/version ]; then
	export LIBVA_DRIVER_NAME=nvidia NVD_BACKEND=direct MOZ_DISABLE_RDD_SANDBOX=1
fi
[ -n "${XDG_RUNTIME_DIR:-}" ] && export SSH_AUTH_SOCK=$XDG_RUNTIME_DIR/ssh-agent.socket
export PATH
unset d
