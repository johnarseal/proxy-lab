#!/bin/bash
#
# driver.sh - This is a simple autograder for the Proxy Lab. It does
#     basic sanity checks that determine whether or not the code
#     behaves like a concurrent caching proxy.
#
#     David O'Hallaron, Carnegie Mellon University
#     updated: 12/09/2013
#
#     Stanley Zhang <szz@andrew.cmu.edu>
#     updated: 4/6/2017
#     Added full key.txt checking
#
#     Stanley Zhang <szz@andrew.cmu.edu>
#     updated: 11/11/2017
#     Added new tiny-static caching test
#
#     usage: ./driver.sh
#

# Point values
MAX_BASIC=40
MAX_CONCURRENCY=15
MAX_CACHE=15

# Various constants
HOME_DIR=`pwd`
PROXY_DIR="./.proxy"
NOPROXY_DIR="./.noproxy"
TIMEOUT=5
MAX_RAND=63000
PORT_START=1024
PORT_MAX=65000
MAX_PORT_TRIES=10

# List of text and binary files for the basic test
BASIC_LIST="home.html
            csapp.c
            tiny.c
            godzilla.jpg
            tiny"

# List of text files for the cache test
CACHE_LIST="tiny.c
            home.html
            csapp.c"

# The file we will fetch for various tests
FETCH_FILE="home.html"

# Number of requests to tiny-static for cache tests
CACHE_STATIC_REQS=20

# The static file that tiny-static serves
STATIC_FILE="home.html"

#####
# Helper functions
#

#
# check_key - Checks key.txt for correct key
# usage: check_key
# returns 255 if the key is malformed, or 0 otherwise.
#
function check_key {
    local -a lines
    mapfile -n 2 -t lines < 'key.txt'
    local andrewid="${lines[0]}"
    local key="${lines[1]}"

    if [[ $andrewid == "Enter your andrew id here" ]]
    then
        echo "Error: key.txt: Missing andrew id."
        return 255
    fi

    if [[ $key == "Enter the key from TheProjectZone here" ]]
    then
        echo "Error: key.txt: Missing key."
        return 255
    fi

    return 0
}

#
# download_proxy - download a file from the origin server via the proxy
# usage: download_proxy <testdir> <filename> <origin_url> <proxy_url>
#
function download_proxy {
    cd $1
    curl --max-time ${TIMEOUT} --silent --proxy $4 --output $2 $3
    (( $? == 28 )) && echo "Error: Fetch timed out after ${TIMEOUT} seconds"
    cd $HOME_DIR
}

#
# download_noproxy - download a file directly from the origin server
# usage: download_noproxy <testdir> <filename> <origin_url>
#
function download_noproxy {
    cd $1
    curl --max-time ${TIMEOUT} --silent --output $2 $3
    (( $? == 28 )) && echo "Error: Fetch timed out after ${TIMEOUT} seconds"
    cd $HOME_DIR
}

#
# clear_dirs - Clear the download directories
#
function clear_dirs {
    rm -rf ${PROXY_DIR}/*
    rm -rf ${NOPROXY_DIR}/*
}

#
# wait_for_port_use - Spins until the TCP port number passed as an
#     argument is actually being used. Times out after 5 seconds.
#
function wait_for_port_use() {
    timeout_count="0"
    portsinuse=`netstat --numeric-ports --numeric-hosts -a --protocol=tcpip \
        | grep tcp | cut -c21- | cut -d':' -f2 | cut -d' ' -f1 \
        | grep -E "[0-9]+" | uniq | tr "\n" " "`

    echo "${portsinuse}" | grep -wq "${1}"
    while [ "$?" != "0" ]
    do
        timeout_count=`expr ${timeout_count} + 1`
        if [ "${timeout_count}" == "${MAX_PORT_TRIES}" ]; then
            kill -ALRM $$
        fi

        sleep 1
        portsinuse=`netstat --numeric-ports --numeric-hosts -a --protocol=tcpip \
            | grep tcp | cut -c21- | cut -d':' -f2 | cut -d' ' -f1 \
            | grep -E "[0-9]+" | uniq | tr "\n" " "`
        echo "${portsinuse}" | grep -wq "${1}"
    done
}


#
# free_port - returns an available unused TCP port
#
function free_port {
    # Generate a random port in the range [PORT_START,
    # PORT_START+MAX_RAND]. This is needed to avoid collisions when many
    # students are running the driver on the same machine.
    port=$((( RANDOM % ${MAX_RAND}) + ${PORT_START}))

    while [ TRUE ]
    do
        portsinuse=`netstat --numeric-ports --numeric-hosts -a --protocol=tcpip \
            | grep tcp | cut -c21- | cut -d':' -f2 | cut -d' ' -f1 \
            | grep -E "[0-9]+" | uniq | tr "\n" " "`

        echo "${portsinuse}" | grep -wq "${port}"
        if [ "$?" == "0" ]; then
            if [ $port -eq ${PORT_MAX} ]
            then
                echo "-1"
                return
            fi
            port=`expr ${port} + 1`
        else
            echo "${port}"
            return
        fi
    done
}


#######
# Main
#######

######
# Verify that we have all of the expected files with the right
# permissions
#

check_key
if [ $? != 0 ]
then
    echo "Error: missing or malformed key.txt. Please re-read the writeup!"
    exit
fi

# Kill any stray proxies or tiny servers owned by this user
killall -q proxy tiny nop-server.py 2> /dev/null

# Make sure we have a Tiny directory
if [ ! -d ./tiny ]
then
    echo "Error: ./tiny directory not found."
    exit
fi

# If there is no Tiny executable, then try to build it
if [ ! -x ./tiny/tiny ]
then
    echo "Building the tiny executable."
    (cd ./tiny; make)
    echo ""
fi

# Make sure we have all the Tiny files we need
if [ ! -x ./tiny/tiny ]
then
    echo "Error: ./tiny/tiny not found or not an executable file."
    exit
fi
for file in ${BASIC_LIST}
do
    if [ ! -e ./tiny/${file} ]
    then
        echo "Error: ./tiny/${file} not found."
        exit
    fi
done

# Make sure we have an existing executable proxy
if [ ! -x ./proxy ]
then
    echo "Error: ./proxy not found or not an executable file. Please rebuild your proxy and try again."
    exit
fi

# Make sure we have an existing executable nop-server.py file
if [ ! -x ./nop-server.py ]
then
    echo "Error: ./nop-server.py not found or not an executable file."
    exit
fi

# Create the test directories if needed
if [ ! -d ${PROXY_DIR} ]
then
    mkdir ${PROXY_DIR}
fi

if [ ! -d ${NOPROXY_DIR} ]
then
    mkdir ${NOPROXY_DIR}
fi

# Add a handler to generate a meaningful timeout message
trap 'echo "Timeout waiting for the server to grab the port reserved for it"; kill $$' ALRM

#####
# Basic
#
echo "*** Basic ***"

# Run the Tiny Web server
tiny_port=$(free_port)
echo "Starting tiny on ${tiny_port}"
cd ./tiny
./tiny ${tiny_port}   &> /dev/null  &
tiny_pid=$!
cd ${HOME_DIR}

# Wait for tiny to start in earnest
wait_for_port_use "${tiny_port}"

# Run the proxy
proxy_port=$(free_port)
echo "Starting proxy on ${proxy_port}"
./proxy ${proxy_port}  &> /dev/null &
proxy_pid=$!

# Wait for the proxy to start in earnest
wait_for_port_use "${proxy_port}"


# Now do the test by fetching some text and binary files directly from
# Tiny and via the proxy, and then comparing the results.
numRun=0
numSucceeded=0
for file in ${BASIC_LIST}
do
    numRun=`expr $numRun + 1`
    echo "${numRun}: ${file}"
    clear_dirs

    # Fetch using the proxy
    echo "   Fetching ./tiny/${file} into ${PROXY_DIR} using the proxy"
    download_proxy $PROXY_DIR ${file} "http://localhost:${tiny_port}/${file}" "http://localhost:${proxy_port}"

    # Fetch directly from Tiny
    echo "   Fetching ./tiny/${file} into ${NOPROXY_DIR} directly from Tiny"
    download_noproxy $NOPROXY_DIR ${file} "http://localhost:${tiny_port}/${file}"

    # Compare the two files
    echo "   Comparing the two files"
    diff -q ${PROXY_DIR}/${file} ${NOPROXY_DIR}/${file} # &> /dev/null
    if [ $? -eq 0 ]; then
        numSucceeded=`expr ${numSucceeded} + 1`
        echo "   Success: Files are identical."
    else
        echo "   Failure: Files differ."
    fi
done

echo "Killing tiny and proxy"
kill $tiny_pid 2> /dev/null
wait $tiny_pid 2> /dev/null
kill $proxy_pid 2> /dev/null
wait $proxy_pid 2> /dev/null

basicScore=`expr ${MAX_BASIC} \* ${numSucceeded} / ${numRun}`

echo "Basic: $basicScore / ${MAX_BASIC}"


######
# Concurrency
#

echo ""
echo "*** Concurrency ***"

# Run the Tiny Web server
tiny_port=$(free_port)
echo "Starting tiny on port ${tiny_port}"
cd ./tiny
./tiny ${tiny_port} &> /dev/null &
tiny_pid=$!
cd ${HOME_DIR}

# Wait for tiny to start in earnest
wait_for_port_use "${tiny_port}"

# Run the proxy
proxy_port=$(free_port)
echo "Starting proxy on port ${proxy_port}"
./proxy ${proxy_port} &> /dev/null &
proxy_pid=$!

# Wait for the proxy to start in earnest
wait_for_port_use "${proxy_port}"

# Run a special blocking nop-server that never responds to requests
nop_port=$(free_port)
echo "Starting the blocking NOP server on port ${nop_port}"
./nop-server.py ${nop_port} &> /dev/null &
nop_pid=$!

# Wait for the nop server to start in earnest
wait_for_port_use "${nop_port}"

# Try to fetch a file from the blocking nop-server using the proxy
clear_dirs
echo "Trying to fetch a file from the blocking nop-server"
download_proxy $PROXY_DIR "nop-file.txt" "http://localhost:${nop_port}/nop-file.txt" "http://localhost:${proxy_port}" &

# Fetch directly from Tiny
echo "Fetching ./tiny/${FETCH_FILE} into ${NOPROXY_DIR} directly from Tiny"
download_noproxy $NOPROXY_DIR ${FETCH_FILE} "http://localhost:${tiny_port}/${FETCH_FILE}"

# Fetch using the proxy
echo "Fetching ./tiny/${FETCH_FILE} into ${PROXY_DIR} using the proxy"
download_proxy $PROXY_DIR ${FETCH_FILE} "http://localhost:${tiny_port}/${FETCH_FILE}" "http://localhost:${proxy_port}"

# See if the proxy fetch succeeded
echo "Checking whether the proxy fetch succeeded"
diff -q ${PROXY_DIR}/${FETCH_FILE} ${NOPROXY_DIR}/${FETCH_FILE} # &> /dev/null
if [ $? -eq 0 ]; then
    concurrencyScore=${MAX_CONCURRENCY}
    echo "Success: Was able to fetch tiny/${FETCH_FILE} from the proxy."
else
    concurrencyScore=0
    echo "Failure: Was not able to fetch tiny/${FETCH_FILE} from the proxy."
fi

# Clean up
echo "Killing tiny, proxy, and nop-server"
kill $tiny_pid 2> /dev/null
wait $tiny_pid 2> /dev/null
kill $proxy_pid 2> /dev/null
wait $proxy_pid 2> /dev/null
kill $nop_pid 2> /dev/null
wait $nop_pid 2> /dev/null

echo "Concurrency: $concurrencyScore / ${MAX_CONCURRENCY}"

#####
# Caching
#
echo ""
echo "*** Cache ***"

cacheScore=0

echo "*** Testing single request ***"

# Run the Tiny Web server
tiny_port=$(free_port)
echo "Starting tiny on port ${tiny_port}"
cd ./tiny
./tiny ${tiny_port} &> /dev/null &
tiny_pid=$!
cd ${HOME_DIR}

# Wait for tiny to start in earnest
wait_for_port_use "${tiny_port}"

# Run the proxy
proxy_port=$(free_port)
echo "Starting proxy on port ${proxy_port}"
./proxy ${proxy_port} &> /dev/null &
proxy_pid=$!

# Wait for the proxy to start in earnest
wait_for_port_use "${proxy_port}"

# Fetch some files from tiny using the proxy
clear_dirs
for file in ${CACHE_LIST}
do
    echo "Fetching ./tiny/${file} into ${PROXY_DIR} using the proxy"
    download_proxy $PROXY_DIR ${file} "http://localhost:${tiny_port}/${file}" "http://localhost:${proxy_port}"
done

# Kill Tiny
echo "Killing tiny"
kill $tiny_pid 2> /dev/null
wait $tiny_pid 2> /dev/null

# Now try to fetch a cached copy of one of the fetched files.
echo "Fetching a cached copy of ./tiny/${FETCH_FILE} into ${NOPROXY_DIR}"
download_proxy $NOPROXY_DIR ${FETCH_FILE} "http://localhost:${tiny_port}/${FETCH_FILE}" "http://localhost:${proxy_port}"

# See if the proxy fetch succeeded by comparing it with the original
# file in the tiny directory
diff -q ./tiny/${FETCH_FILE} ${NOPROXY_DIR}/${FETCH_FILE} # &> /dev/null
if [ $? -eq 0 ]; then
    $((cacheScore += MAX_CACHE / 3))
    echo "Success: Was able to fetch tiny/${FETCH_FILE} from the cache."

    echo "*** Testing multiple requests ***"

    # Run the tiny-static server
    tiny_static_port=$(free_port)
    echo "Starting tiny-static on port ${tiny_static_port}"
    cd ./tiny
    ./tiny-static ${tiny_static_port} &> /dev/null &
    tiny_static_pid=$!
    cd ${HOME_DIR}

    # Wait for tiny-static to start in earnest
    wait_for_port_use "${tiny_static_port}"

    echo "Warming up cache with ${CACHE_STATIC_REQS} unique requests"

    # Fetch many different URLs - they should all be the same.
    for ((n=0; n<=CACHE_STATIC_REQS; n++)); do
        download_proxy $PROXY_DIR /dev/null "http://localhost:${tiny_static_port}/${n}" "http://localhost:${proxy_port}"
    done

    # Kill tiny-static
    echo "Killing tiny-static"
    kill $tiny_static_pid 2> /dev/null
    wait $tiny_static_pid 2> /dev/null

    # Now try to fetch a cached copy of each requested URL.
    echo "Attempting to re-fetch ${CACHE_STATIC_REQS} requests from cache"
    failure=0
    clear_dirs
    for ((n=0; n<=CACHE_STATIC_REQS; n++)); do
        download_proxy $PROXY_DIR ${STATIC_FILE} "http://localhost:${tiny_static_port}/${n}" "http://localhost:${proxy_port}"
        diff -q ./tiny/${STATIC_FILE} ${PROXY_DIR}/${STATIC_FILE}
        if [ $? -ne 0 ]; then
            echo "Failure: was not able to fetch request ${n} from proxy cache"
            failure=1
            break
        fi
        rm ${PROXY_DIR}/${STATIC_FILE}
    done

    if [ $failure -eq 0 ]; then
        echo "Success: fetched all ${CACHE_STATIC_REQS} requests from cache"
        $((cacheScore += MAX_CACHE / 3 * 2))
    fi
else
    echo "Failure: Was not able to fetch tiny/${FETCH_FILE} from the proxy cache."
fi

# Kill the proxy
echo "Killing proxy"
kill $proxy_pid 2> /dev/null
wait $proxy_pid 2> /dev/null

echo "Cache: $cacheScore / ${MAX_CACHE}"

# Emit the total score
totalScore=`expr ${basicScore} + ${cacheScore} + ${concurrencyScore}`
maxScore=`expr ${MAX_BASIC} + ${MAX_CACHE} + ${MAX_CONCURRENCY}`
echo ""
echo "totalScore = ${totalScore} / ${maxScore}"

echo ""
echo "{ \"scores\": {\"Basic\":${basicScore}, \"Concurrency\":${concurrencyScore}, \"Caching\":${cacheScore}}, \"scoreboard\": [${totalScore}, ${basicScore}, ${concurrencyScore}, ${cacheScore}]}"

exit

