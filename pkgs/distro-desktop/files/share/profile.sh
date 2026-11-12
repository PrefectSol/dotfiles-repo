d=${XDG_DATA_HOME:-$HOME/.local/share}/distro
case ":$PATH:" in
*":$d/gcc/bin:"*) ;;
*) PATH=$HOME/.local/bin:$d/gcc/bin:$d/cuda/bin:$PATH ;;
esac
if [ -L "$d/cuda" ]; then
	export CUDA_HOME=$d/cuda CUDA_PATH=$d/cuda
	export LD_LIBRARY_PATH=$d/cuda/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
fi
export PATH
unset d
