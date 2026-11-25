# Pinkcoin-Qt, as pinkcoin-qt.pro builds it.
#
# qmake defines QT_GUI for every source it compiles, and wallet.cpp and init.cpp
# test it, so the GUI compiles the core sources itself, with QT_GUI, rather than
# linking pink2_core (built without). The shared defines, includes and libraries
# come from pink2_core_defs.

set(CMAKE_AUTOMOC ON)
# AUTOUIC and AUTORCC are off. AUTOUIC takes the real header ui_interface.h
# (included by wallet.h) for a generated one, so the forms go through
# qt5_wrap_ui; the resource file is generated below, with the .qm files in the
# build tree.
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)


# Sources
# =======

# SOURCES of pinkcoin-qt.pro less the core sources (PINK2_CORE_SOURCES, which
# carry txdb-leveldb.cpp and the scrypt assembly) and init.cpp/noui.cpp.
set(PINK2_QT_SOURCES
    bitcoin.cpp
    bitcoingui.cpp
    transactiontablemodel.cpp
    addresstablemodel.cpp
    optionsdialog.cpp
    sendcoinsdialog.cpp
    coincontroldialog.cpp
    coincontroltreewidget.cpp
    addressbookpage.cpp
    signverifymessagedialog.cpp
    aboutdialog.cpp
    editaddressdialog.cpp
    bitcoinaddressvalidator.cpp
    clientmodel.cpp
    guiutil.cpp
    transactionrecord.cpp
    optionsmodel.cpp
    monitoreddatamapper.cpp
    transactiondesc.cpp
    transactiondescdialog.cpp
    bitcoinstrings.cpp
    bitcoinamountfield.cpp
    transactionfilterproxy.cpp
    transactionview.cpp
    walletmodel.cpp
    overviewpage.cpp
    csvmodelwriter.cpp
    sendcoinsentry.cpp
    qvalidatedlineedit.cpp
    bitcoinunits.cpp
    qvaluecombobox.cpp
    askpassphrasedialog.cpp
    notificator.cpp
    qtipcserver.cpp
    rpcconsole.cpp
    trafficgraphwidget.cpp
    messagepage.cpp
    messagemodel.cpp
    sendmessagesdialog.cpp
    sendmessagesentry.cpp
    qvalidatedtextedit.cpp
    plugins/mrichtexteditor/mrichtextedit.cpp
)

# HEADERS of pinkcoin-qt.pro, relative to src/. Listed for AUTOMOC.
set(PINK2_QT_HEADERS
    qt/bitcoingui.h
    qt/transactiontablemodel.h
    qt/addresstablemodel.h
    qt/optionsdialog.h
    qt/coincontroldialog.h
    qt/coincontroltreewidget.h
    qt/sendcoinsdialog.h
    qt/addressbookpage.h
    qt/signverifymessagedialog.h
    qt/aboutdialog.h
    qt/editaddressdialog.h
    qt/bitcoinaddressvalidator.h
    alert.h
    addrman.h
    base58.h
    bignum.h
    checkpoints.h
    compat.h
    coincontrol.h
    sync.h
    util.h
    uint256.h
    kernel.h
    stakeproofs.h
    scrypt.h
    pbkdf2.h
    serialize.h
    smessage.h
    main.h
    miner.h
    net.h
    key.h
    db.h
    txdb.h
    walletdb.h
    stakedb.h
    script.h
    stealth.h
    init.h
    mruset.h
    json/json_spirit_writer_template.h
    json/json_spirit_writer.h
    json/json_spirit_value.h
    json/json_spirit_utils.h
    json/json_spirit_stream_reader.h
    json/json_spirit_reader_template.h
    json/json_spirit_reader.h
    json/json_spirit_error_position.h
    json/json_spirit.h
    qt/clientmodel.h
    qt/guiutil.h
    qt/transactionrecord.h
    qt/guiconstants.h
    qt/optionsmodel.h
    qt/monitoreddatamapper.h
    qt/transactiondesc.h
    qt/transactiondescdialog.h
    qt/bitcoinamountfield.h
    wallet.h
    keystore.h
    qt/transactionfilterproxy.h
    qt/transactionview.h
    qt/walletmodel.h
    bitcoinrpc.h
    qt/overviewpage.h
    qt/csvmodelwriter.h
    crypter.h
    qt/sendcoinsentry.h
    qt/qvalidatedlineedit.h
    qt/bitcoinunits.h
    qt/qvaluecombobox.h
    qt/askpassphrasedialog.h
    protocol.h
    qt/notificator.h
    qt/qtipcserver.h
    allocators.h
    ui_interface.h
    qt/rpcconsole.h
    qt/trafficgraphwidget.h
    version.h
    netbase.h
    clientversion.h
    threadsafety.h
    qt/messagepage.h
    qt/messagemodel.h
    qt/sendmessagesdialog.h
    qt/sendmessagesentry.h
    qt/plugins/mrichtexteditor/mrichtextedit.h
    qt/qvalidatedtextedit.h
    ntp.h
)

set(PINK2_QT_FORMS
    forms/coincontroldialog.ui
    forms/sendcoinsdialog.ui
    forms/addressbookpage.ui
    forms/signverifymessagedialog.ui
    forms/aboutdialog.ui
    forms/editaddressdialog.ui
    forms/transactiondescdialog.ui
    forms/overviewpage.ui
    forms/sendcoinsentry.ui
    forms/askpassphrasedialog.ui
    forms/rpcconsole.ui
    forms/optionsdialog.ui
    forms/messagepage.ui
    forms/sendmessagesentry.ui
    forms/sendmessagesdialog.ui
    plugins/mrichtexteditor/mrichtextedit.ui
)

# contains(USE_QRCODE, 1)
if(ENABLE_QRENCODE)
    list(APPEND PINK2_QT_SOURCES qrcodedialog.cpp)
    list(APPEND PINK2_QT_HEADERS qt/qrcodedialog.h)
    list(APPEND PINK2_QT_FORMS forms/qrcodedialog.ui)
endif()

# macx
if(APPLE)
    list(APPEND PINK2_QT_HEADERS qt/macdockiconhandler.h qt/macnotificationhandler.h)
    set(PINK2_QT_OBJC_SOURCES macdockiconhandler.mm macnotificationhandler.mm)
    list(TRANSFORM PINK2_QT_OBJC_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
endif()

# Sources and forms are relative to src/qt, headers to src/.
list(TRANSFORM PINK2_QT_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(TRANSFORM PINK2_QT_FORMS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(TRANSFORM PINK2_QT_HEADERS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/../")
# init.cpp and noui.cpp are in the .pro's SOURCES, not in PINK2_CORE_SOURCES.
set(PINK2_QT_CORE_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/../init.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../noui.cpp")


# Forms: ui_<name>.h land in this directory's binary dir (qmake's UI_DIR).
qt5_wrap_ui(PINK2_UI_HEADERS ${PINK2_QT_FORMS})


# Translations and resources
# ==========================

# The .pro runs lrelease over every bitcoin_*.ts into src/qt/locale, in the
# source tree. Here the .qm files are built in the build tree, all of them, and
# the resource file the GUI compiles is a copy of bitcoin.qrc in which the
# locale entries point there and every other entry at its file in the source
# tree. The qrc embeds only the translations it names (39 of the 65).
file(GLOB PINK2_TS_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/locale/bitcoin_*.ts")
list(SORT PINK2_TS_FILES)
set(PINK2_QM_DIR "${CMAKE_CURRENT_BINARY_DIR}/locale")
file(MAKE_DIRECTORY "${PINK2_QM_DIR}")
set_source_files_properties(${PINK2_TS_FILES} PROPERTIES OUTPUT_LOCATION "${PINK2_QM_DIR}")
qt5_add_translation(PINK2_QM_FILES ${PINK2_TS_FILES})

set(PINK2_QRC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/bitcoin.qrc")
set(PINK2_QRC_PROCESSED "${CMAKE_CURRENT_BINARY_DIR}/bitcoin.qrc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PINK2_QRC_SOURCE}")
file(READ "${PINK2_QRC_SOURCE}" _qrc)
string(REGEX REPLACE ">locale/" ">${PINK2_QM_DIR}/" _qrc "${_qrc}")
string(REGEX REPLACE ">res/" ">${CMAKE_CURRENT_SOURCE_DIR}/res/" _qrc "${_qrc}")
# Written at configure time, so that qt5_add_resources can read its file list.
file(WRITE "${PINK2_QRC_PROCESSED}" "${_qrc}")
qt5_add_resources(PINK2_QRC_CPP "${PINK2_QRC_PROCESSED}")


# Target
# ======

add_executable(Pinkcoin-Qt
    ${PINK2_CORE_SOURCES}
    ${PINK2_QT_CORE_EXTRA}
    ${PINK2_QT_SOURCES}
    ${PINK2_QT_HEADERS}
    ${PINK2_QT_FORMS}
    ${PINK2_UI_HEADERS}
    ${PINK2_QRC_CPP}
    ${PINK2_QM_FILES}
)
# The GUI compiles version.cpp itself, which includes the generated build.h.
add_dependencies(Pinkcoin-Qt pink2_build_info)
set_target_properties(Pinkcoin-Qt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# INCLUDEPATH src src/json src/qt src/qt/plugins/mrichtexteditor
target_include_directories(Pinkcoin-Qt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../json"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/mrichtexteditor"
    "${CMAKE_CURRENT_BINARY_DIR}"
)

# DEFINES QT_GUI BOOST_THREAD_USE_LIB BOOST_SPIRIT_THREADSAFE (the last is in
# pink2_core_defs), and QT_DISABLE_DEPRECATED_BEFORE=0 for Qt 5.
target_compile_definitions(Pinkcoin-Qt PRIVATE
    QT_GUI BOOST_THREAD_USE_LIB QT_DISABLE_DEPRECATED_BEFORE=0)

target_link_libraries(Pinkcoin-Qt PRIVATE
    pink2_core_defs
    Qt5::Widgets Qt5::Network Qt5::Svg
    ${PINK2_RUNTIME_LIBS}
)

# USE_QRCODE
if(ENABLE_QRENCODE)
    target_compile_definitions(Pinkcoin-Qt PRIVATE USE_QRCODE)
    target_link_libraries(Pinkcoin-Qt PRIVATE QREncode::QREncode)
endif()

# USE_UPNP: the .pro adds STATICLIB here, and -liphlpapi on Windows.
if(ENABLE_UPNP)
    target_compile_definitions(Pinkcoin-Qt PRIVATE STATICLIB)
    if(WIN32)
        target_link_libraries(Pinkcoin-Qt PRIVATE iphlpapi)
    endif()
endif()

# USE_DBUS
if(USE_DBUS)
    target_compile_definitions(Pinkcoin-Qt PRIVATE USE_DBUS)
    target_link_libraries(Pinkcoin-Qt PRIVATE Qt5::DBus)
endif()

# A static build imports the Qt plugins bitcoin.cpp names (qcncodecs qjpcodecs
# qtwcodecs qkrcodecs qtaccessiblewidgets qsvg qsvgicon). The plugin libraries
# come with the static Qt of the release builds.
if(STATIC_LIBS)
    target_compile_definitions(Pinkcoin-Qt PRIVATE STATICLIB BITCOIN_NEED_QT_PLUGINS)
endif()

# !windows:!macx
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(Pinkcoin-Qt PRIVATE LINUX)
endif()

# windows: WIN32 and _MT come with pink2_core_defs.
if(WIN32)
    target_link_libraries(Pinkcoin-Qt PRIVATE
        ws2_32 shlwapi mswsock ole32 oleaut32 uuid gdi32)
    target_link_options(Pinkcoin-Qt PRIVATE -Wl,--dynamicbase -Wl,--nxcompat)
    set_target_properties(Pinkcoin-Qt PROPERTIES WIN32_EXECUTABLE ON)
    target_sources(Pinkcoin-Qt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/res/bitcoin-qt.rc")
endif()

# macx: the Objective-C++ handlers, frameworks, icon, Info.plist from the
# template.
if(APPLE)
    target_sources(Pinkcoin-Qt PRIVATE ${PINK2_QT_OBJC_SOURCES})
    target_link_libraries(Pinkcoin-Qt PRIVATE
        "-framework Foundation" "-framework ApplicationServices" "-framework AppKit")
    set(PINK2_MAC_ICON "${CMAKE_CURRENT_SOURCE_DIR}/res/icons/pinkcoin.icns")
    target_sources(Pinkcoin-Qt PRIVATE "${PINK2_MAC_ICON}")
    set_source_files_properties("${PINK2_MAC_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(Pinkcoin-Qt PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/share/qt/Info.plist.cmake.in")
endif()

# themes: next to the binary, as the .pro's INSTALLS puts them. Not in the macOS
# bundle: the .pro puts them in Contents/MacOS, where codesign accepts only code
# ("code object is not signed at all" on the first .png), no GUI source reads
# them (bitcoingui.h declares themesDir and never uses it), and 2.4.0.0's DMG did
# not contain them.
if(NOT APPLE)
    add_custom_command(TARGET Pinkcoin-Qt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/res/themes" "$<TARGET_FILE_DIR:Pinkcoin-Qt>/themes"
        VERBATIM)
endif()


# macOS deploy target
# ===================
#
# cmake --build build --target deploy writes Pinkcoin-macOS-<arch>-<version>.dmg
# to the build directory: the bundle with the Qt frameworks and plugins and the
# other libraries it loads copied in and relocated by
# contrib/macdeploy/macdeployqtplus (Gridcoin's), and code-signed. The signing
# identity is APPLE_SIGNING_IDENTITY from the environment the target runs in
# (not passed on the command line: identities contain parentheses); without it
# the bundle is signed ad hoc. There is no entitlements file, so a real identity
# signs without the hardened runtime. contrib/devtools/check-macos-bundle.sh then
# checks the DMG's app against the floor.
if(APPLE)
    if(NOT Python3_EXECUTABLE)
        find_program(Python3_EXECUTABLE NAMES python3)
    endif()
    if(NOT Python3_EXECUTABLE)
        set(Python3_EXECUTABLE "python3")
    endif()

    # Qt's paths come from the qmake of the Qt this build found. Gridcoin asks
    # the qtpaths on PATH, which with Qt 5 can belong to another Qt (Homebrew's
    # Qt 6 links its qtpaths into the prefix's bin; qt@5 is keg-only), and Qt 5's
    # qtpaths has no --query.
    get_target_property(_qt_qmake Qt5::qmake IMPORTED_LOCATION)
    foreach(_qt_query PREFIX PLUGINS TRANSLATIONS)
        execute_process(COMMAND "${_qt_qmake}" -query QT_INSTALL_${_qt_query}
            OUTPUT_VARIABLE _qt_install_${_qt_query}
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    endforeach()

    # The architecture the bundle is for: CMAKE_OSX_ARCHITECTURES when it names
    # one, otherwise the host's (arm64 or x86_64 on the native runners).
    list(LENGTH CMAKE_OSX_ARCHITECTURES _osx_arch_count)
    if(_osx_arch_count EQUAL 1)
        set(_dmg_arch "${CMAKE_OSX_ARCHITECTURES}")
    else()
        set(_dmg_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    set(_dmg_name "Pinkcoin-macOS-${_dmg_arch}-${PINK2_VERSION}")

    # The volume is named Pinkcoin-Qt and holds Pinkcoin-Qt.app, as 2.4.0.0's.
    set(_deploy_cmd
        "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/contrib/macdeploy/macdeployqtplus"
        "$<TARGET_BUNDLE_DIR:Pinkcoin-Qt>" Pinkcoin-Qt
        -dmg "${_dmg_name}")
    if(IS_DIRECTORY "${_qt_install_PLUGINS}")
        list(APPEND _deploy_cmd -plugin-dir "${_qt_install_PLUGINS}")
    else()
        message(WARNING "deploy: no Qt plugin directory from ${_qt_qmake}; macdeployqtplus will guess")
    endif()
    if(IS_DIRECTORY "${_qt_install_TRANSLATIONS}")
        list(APPEND _deploy_cmd -translations-dir "${_qt_install_TRANSLATIONS}")
    endif()

    add_custom_target(deploy
        COMMAND ${CMAKE_COMMAND} -E env "QTDIR=${_qt_install_PREFIX}" ${_deploy_cmd}
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Deploying ${_dmg_name}.dmg via macdeployqtplus"
        VERBATIM)
    add_dependencies(deploy Pinkcoin-Qt)
endif()


# Tests
# =====

if(ENABLE_TESTS)
    add_test(NAME qt_translations
        COMMAND ${CMAKE_COMMAND}
            -DQRC_SOURCE=${PINK2_QRC_SOURCE}
            -DQRC_PROCESSED=${PINK2_QRC_PROCESSED}
            -DRCC=$<TARGET_FILE:Qt5::rcc>
            -DTS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/locale
            -DQM_DIR=${PINK2_QM_DIR}
            -DEXPECTED_COUNT=39
            -P "${CMAKE_SOURCE_DIR}/build-aux/cmake/tests/check_qt_translations.cmake")
endif()
