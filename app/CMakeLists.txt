cmake_minimum_required(VERSION 3.4.1)

file( GLOB app_src_files
     "src/main/cpp/*.cpp" )

add_library( native-lib SHARED ${app_src_files} )

find_library( log-lib log )
find_library( camera-lib camera2ndk )
find_library( media-lib mediandk )
find_library( android-lib android )
find_library( gl-lib GLESv2 )
target_link_libraries( native-lib  ${log-lib} ${camera-lib} ${media-lib} ${android-lib} ${gl-lib})
