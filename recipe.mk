INC-y += /usr/include/gstreamer-1.0 \
         /usr/include/glib-2.0 \
		 /usr/lib/x86_64-linux-gnu/glib-2.0/include \
		 ./

LIB-y += pthread gstreamer-1.0 glib-2.0  gobject-2.0 
OBJ-y += gst_aplay \
         thread_pool \
		 mpegts_parser 

