

# check to see if dependencies are installed
if ! command -v g++ &> /dev/null; then
    sudo apt update
    sudo apt install build-essential
fi
    
# check for c++20 support
if ! g++ --version | grep "c++20" &> /dev/null; then
    sudo apt update
    sudo apt install g++-13
fi
    

# checkout for gtest
if ! command -v gtest &> /dev/null; then
    sudo apt update
    sudo apt install libgtest-dev
fi
