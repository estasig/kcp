#ffmpeg -pix_fmt uyvy422 -fflags nobuffer -framerate 30 -f avfoundation -i "0" -fflags nobuffer -pix_fmt yuv420p -vcodec libx264 -preset ultrafast -tune zerolatency  -b:v 1M -maxrate 1M -g 30 -an -f mpegts tcp://127.0.0.1:9001
ffmpeg -pix_fmt uyvy422 -fflags nobuffer -framerate 30 -f avfoundation -i "0" -fflags nobuffer -pix_fmt yuv420p -vcodec libx264 -preset ultrafast -tune zerolatency  -g 30 -an -f mpegts tcp://127.0.0.1:9001