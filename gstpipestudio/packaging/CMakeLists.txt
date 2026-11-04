# What is installed besides the programs: the menu entry, the icons, the licence, and the links in bin/.
install(FILES gstpipestudio.desktop DESTINATION share/applications)
install(DIRECTORY icons/hicolor DESTINATION share/icons)
install(FILES ${GPS_ROOT}/../LICENSE DESTINATION share/doc/gstpipestudio RENAME copyright)

# The programs are in GPS_PRIVATE_DIR/bin (see the top CMakeLists.txt); bin/ gets a link to each, so they are on the PATH.
# A program started through its link still looks for its Qt and its helpers where it really is.
foreach(tool gstpipestudio gps-inspect gps-update gps-run)
    install(CODE "
        file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin\")
        file(CREATE_LINK ../${GPS_PRIVATE_DIR}/bin/${tool} \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/${tool}\" SYMBOLIC)
    ")
endforeach()
