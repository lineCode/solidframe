if(WIN32)
    #find_path(BOOST_ROOT NAMES boost/asio.hpp PATHS "${EXTERNAL_DIR}/include/boost-1_66")
    file(GLOB BOOST_PATH "${EXTERNAL_DIR}/include/boost-*/boost/asio.hpp")
    get_filename_component(BOOST_ROOT "${BOOST_PATH}" DIRECTORY)
    get_filename_component(BOOST_ROOT "${BOOST_ROOT}" DIRECTORY)
    include_directories("${BOOST_ROOT}")
    set(Boost_FOUND TRUE)
else()
    set(BOOST_ROOT "${EXTERNAL_DIR}")
    set(Boost_USE_STATIC_LIBS ON)
    set(Boost_USE_MULTITHREADED ON)
    set(Boost_USE_STATIC_RUNTIME ON)

    find_package(Boost
        COMPONENTS
            system
            filesystem
            date_time
            program_options
            serialization
    )
endif(WIN32)
