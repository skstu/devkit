#ifndef CVPP_DEPS_H
#define CVPP_DEPS_H


#pragma comment(lib,"ippicvmt.lib")

#if _DEBUG
#pragma comment(lib,"opencv_calib3d4100d.lib")
#pragma comment(lib,"opencv_core4100d.lib")
#pragma comment(lib,"opencv_dnn4100d.lib")
#pragma comment(lib,"opencv_features2d4100d.lib")
#pragma comment(lib,"opencv_flann4100d.lib")
#pragma comment(lib,"opencv_gapi4100d.lib")
#pragma comment(lib,"opencv_highgui4100d.lib")
#pragma comment(lib,"opencv_imgcodecs4100d.lib")
#pragma comment(lib,"opencv_imgproc4100d.lib")
#pragma comment(lib,"opencv_ml4100d.lib")
#pragma comment(lib,"opencv_objdetect4100d.lib")
#pragma comment(lib,"opencv_photo4100d.lib")
#pragma comment(lib,"opencv_stitching4100d.lib")
#pragma comment(lib,"opencv_ts4100d.lib")
#pragma comment(lib,"opencv_video4100d.lib")
#pragma comment(lib,"opencv_videoio4100d.lib")

#pragma comment(lib,"aded.lib")
#pragma comment(lib,"IlmImfd.lib")
#pragma comment(lib,"ippiwd.lib")
#pragma comment(lib,"ittnotifyd.lib")
#pragma comment(lib,"libjpeg-turbod.lib")
#pragma comment(lib,"libopenjp2d.lib")
#pragma comment(lib,"libpngd.lib")
#pragma comment(lib,"libprotobufd.lib")
#pragma comment(lib,"libtiffd.lib")
#pragma comment(lib,"libwebpd.lib")
#pragma comment(lib,"zlibd.lib")
#else
#pragma comment(lib,"opencv_calib3d4100.lib")
#pragma comment(lib,"opencv_core4100.lib")
#pragma comment(lib,"opencv_dnn4100.lib")
#pragma comment(lib,"opencv_features2d4100.lib")
#pragma comment(lib,"opencv_flann4100.lib")
#pragma comment(lib,"opencv_gapi4100.lib")
#pragma comment(lib,"opencv_highgui4100.lib")
#pragma comment(lib,"opencv_imgcodecs4100.lib")
#pragma comment(lib,"opencv_imgproc4100.lib")
#pragma comment(lib,"opencv_ml4100.lib")
#pragma comment(lib,"opencv_objdetect4100.lib")
#pragma comment(lib,"opencv_photo4100.lib")
#pragma comment(lib,"opencv_stitching4100.lib")
#pragma comment(lib,"opencv_ts4100.lib")
#pragma comment(lib,"opencv_video4100.lib")
#pragma comment(lib,"opencv_videoio4100.lib")

#pragma comment(lib,"ade.lib")
#pragma comment(lib,"IlmImf.lib")
#pragma comment(lib,"ippiw.lib")
#pragma comment(lib,"ittnotify.lib")
#pragma comment(lib,"libjpeg-turbo.lib")
#pragma comment(lib,"libopenjp2.lib")
#pragma comment(lib,"libpng.lib")
#pragma comment(lib,"libprotobuf.lib")
#pragma comment(lib,"libtiff.lib")
#pragma comment(lib,"libwebp.lib")
#pragma comment(lib,"zlib.lib")
#endif


#endif //CVPP_DEPS_H
