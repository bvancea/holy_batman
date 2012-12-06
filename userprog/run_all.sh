make clean &&
make &&
cd build && 
pintos --filesys-size=20 -p tests/userprog/args-many -a args-many -- -f -q extract run 'args-many a b c d e f g h i j k l m n o p q r s t u v' &&
cd ..
