#include <gst/gst.h>
#include <stdio.h>

static void pad_added_handler(GstElement *el_src, GstPad *new_pad, gpointer user_data);
static gboolean low_bus_cb (GstBus *bus, GstMessage *msg, gpointer user_data);
static gboolean high_bus_cb (GstBus *bus, GstMessage *msg, gpointer user_data);

GstElement *lowsrc, *highsrc;
GstElement *lowdepay, *highdepay;
GstElement *lowrespipe, *highrespipe;

int main(int argc, char* argv[]){
	GMainLoop *loop;	
	GstElement *lowparse, *lowdec, *lowconvbefore, *lowmcells, *lowconvafter, *lowfilter, *lowsink;
	GstElement *highparse, *highdec, *highconv, *highsink;

	//check input parameters given
	if(argc <=2){
		g_printerr("rtsp addresses not given\n");
		g_printerr("usage is ./dualpipeline <low-res-rtsp> <high-res-rtsp>\n");
		g_printerr("include authentication if neccessary\n");
		return -1;
	}

	//init gstreamer
	gst_init(&argc, &argv);

	//create elements
	loop = g_main_loop_new(NULL, FALSE);

	lowrespipe = gst_pipeline_new("low-res-pipeline");
	highrespipe = gst_pipeline_new("high-res-pipeline");

	lowsrc = gst_element_factory_make("rtspsrc", "lowsrc");
	lowdepay = gst_element_factory_make("rtph264depay", NULL);
	lowparse = gst_element_factory_make("h264parse", NULL);
	lowdec = gst_element_factory_make("avdec_h264", NULL);
	lowconvbefore = gst_element_factory_make("videoconvert", NULL);
	lowmcells = gst_element_factory_make("motioncells", "mcells");
	lowconvafter = gst_element_factory_make("videoconvert", NULL);
	lowfilter = gst_element_factory_make("capsfilter", NULL);
	lowsink = gst_element_factory_make("xvimagesink", "lowsink");

	highsrc	= gst_element_factory_make("rtspsrc", "highsrc");
	highdepay = gst_element_factory_make("rtph264depay", NULL);
	highparse = gst_element_factory_make("h264parse", NULL);
	highdec = gst_element_factory_make("avdec_h264", NULL);	
	highconv = gst_element_factory_make("videoconvert", NULL);
	highsink = gst_element_factory_make("xvimagesink", "highsink");

	/*
		rtspsrc cannot be linked straight away, connect to pad-added-handler
		which will negotiate and link pads
	*/

	g_signal_connect(lowsrc, "pad-added", G_CALLBACK(pad_added_handler), loop);
	g_signal_connect(highsrc, "pad-added", G_CALLBACK(pad_added_handler), loop);
	g_object_set(lowsrc, "location", argv[1],  NULL);
	g_object_set(highsrc, "location", argv[2], NULL);

    //set filter caps
    gst_util_set_object_arg(G_OBJECT(lowfilter), "caps",
    	"video/x-raw,format=I420, framerate, GST_TYPE_FRACTION, 10, 1");

    //add elements to bins and link all but src
    gst_bin_add_many(GST_BIN(lowrespipe), lowsrc, lowdepay, lowparse, lowdec, lowconvbefore, lowmcells, lowconvafter, lowsink, NULL);
    gst_bin_add_many(GST_BIN(highrespipe), highsrc, highdepay, highparse, highdec, highconv, highsink, NULL);

    gst_element_link_many(lowdepay, lowparse, lowdec, lowconvbefore, lowmcells, lowconvafter,  lowsink, NULL);
    gst_element_link_many(highdepay, highparse, highdec, highconv, highsink, NULL);

    //start playing
    gst_element_set_state(lowrespipe, GST_STATE_PLAYING);
    gst_element_set_state(highrespipe, GST_STATE_PLAYING);

    gst_bus_add_watch(GST_ELEMENT_BUS(lowrespipe), low_bus_cb, loop);
    gst_bus_add_watch(GST_ELEMENT_BUS(highrespipe), high_bus_cb, loop);

    g_main_loop_run(loop);

    //once loop exited, dispose of pipelines
    gst_element_set_state(lowrespipe, GST_STATE_NULL);
    gst_element_set_state(highrespipe, GST_STATE_NULL);

    gst_object_unref(lowrespipe);
    gst_object_unref(highrespipe);

    return 0;
}

static void pad_added_handler(GstElement *src, GstPad *new_pad, gpointer user_data){
  GstPadLinkReturn ret;
  GstEvent *event;
  GstPad *depay_pad;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;
  GstCaps *filter = NULL;

  g_print("Received new pad '%s' from '%s'\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));
  //if pad already linked, nothing to do
  if(gst_pad_is_linked(new_pad)){
    g_print(" pad linked, ignoring...\n");
    goto exit;
  }

  //get the correct depay pad
  if(GST_OBJECT_NAME(src) == GST_OBJECT_NAME(lowsrc)){
  	depay_pad =  gst_element_get_static_pad(lowdepay, "sink");  
  }else{
  	depay_pad =  gst_element_get_static_pad(highdepay, "sink");  
  }  

  filter = gst_caps_from_string("application/x-rtp");
  new_pad_caps = gst_pad_query_caps(new_pad, filter);

  //send reconfigure event
  event = gst_event_new_reconfigure();
  gst_pad_send_event(new_pad, event);

  //check new pad type
  new_pad_struct = gst_caps_get_structure(new_pad_caps,0);
  new_pad_type = gst_structure_get_name(new_pad_struct);
  if(!g_str_has_prefix(new_pad_type, "application/x-rtp")){
    g_print(" Type: '%s', looking for rtp. Ignoring\n",new_pad_type);
    goto exit;
  }

  //attempt to link
  g_print("Attempting to link source pad '%s' to sink pad '%s'\n",GST_PAD_NAME(new_pad), GST_PAD_NAME(depay_pad));
  ret = gst_pad_link(new_pad, depay_pad);
  if(GST_PAD_LINK_FAILED(ret)){
    g_print(" Type is: '%s' but link failed.\n", new_pad_type);
  }else{
    g_print(" Link Succeeded (type: '%s')\n", new_pad_type);       
  } 

  exit:
    //unref new pad caps if required
    if(new_pad_caps != NULL){
      gst_caps_unref(new_pad_caps);
    }
    //unref depay pad
    gst_object_unref(depay_pad);    
}



static gboolean low_bus_cb (GstBus *bus, GstMessage *msg, gpointer user_data){
	GMainLoop *loop = user_data;

	//parse bus messages
	switch(GST_MESSAGE_TYPE(msg)){
		case GST_MESSAGE_ERROR:{
			//quit on error
			GError *err = NULL;
			gchar *dbg;
			gst_message_parse_error(msg, &err, &dbg);
			gst_object_default_error(msg->src, err, dbg);
			g_clear_error(&err);
			g_free(dbg);
			g_main_loop_quit(loop);
			break;
		}
		case GST_MESSAGE_STATE_CHANGED:{
			GstState old_state, pending_state, new_state;
			gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
			if(GST_OBJECT_NAME(msg->src) == GST_OBJECT_NAME(lowrespipe)){
				g_print("'%s' state changed from %s to %s. \n", GST_OBJECT_NAME(msg->src), gst_element_state_get_name(old_state), gst_element_state_get_name(new_state)); 
			}
			break;
		}
		default:{
			break;
		}
	}
	return TRUE;
}

static gboolean high_bus_cb (GstBus *bus, GstMessage *msg, gpointer user_data){
	GMainLoop *loop = user_data;

	//parse bus messages
	switch(GST_MESSAGE_TYPE(msg)){
		case GST_MESSAGE_ERROR:{
			//quit on error
			GError *err = NULL;
			gchar *dbg;
			gst_message_parse_error(msg, &err, &dbg);
			gst_object_default_error(msg->src, err, dbg);
			g_clear_error(&err);
			g_free(dbg);
			g_main_loop_quit(loop);
			break;
		}
		case GST_MESSAGE_STATE_CHANGED:{
			GstState old_state, pending_state, new_state;
			gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
			if(GST_OBJECT_NAME(msg->src) == GST_OBJECT_NAME(lowrespipe)){
				g_print("'%s' state changed from %s to %s. \n", GST_OBJECT_NAME(msg->src), gst_element_state_get_name(old_state), gst_element_state_get_name(new_state)); 
			}
			break;
		}
		default:{
			break;
		}
	}
	return TRUE;
}

