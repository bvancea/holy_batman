cd ../examples &&
make clean &&
make &&
make clean &&
make &&
cd ../userprog &&
cd build && 
pintos --filesys-size=20 -p tests/userprog/write_normal -a bad-read -- -f -q extract run 'write_normal' &&
#pintos --filesys-size=20 -p ../../examples/my_userprog_test.c -a my_userprog_test -- -f -q extract run 'my_userprog_test' &&
cd ..
