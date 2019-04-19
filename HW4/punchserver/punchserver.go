package main

import (
	"fmt"
	"log"
	"net"
	"os"
	"regexp"
	"time"
)

type connection struct {
	uname   string // username
	owport  string // outward facing port
	cliport string // punch client facing port
	cliip   string // client ip address
	bytrcv  int    // bytes received
	bytsnd  int    // bytes sent
}

var openConns map[string]connection // maps of open connections

var optSplit = regexp.MustCompile(`\s+`)

/*
	Open a
	owconn: outward connection
	ctrconn: control connection
*/
func pipe(owconn net.Conn, cliconn net.Conn) {

}

func openPort(ctrconn net.Conn, port string, uname string) {
	l, err := net.Listen("tcp", ":"+port)

	if err != nil {
		log.Fatal("open app port error:", err)
		ctrconn.Write([]byte("FAIL"))
		ctrconn.Close()
		return
	}

	defer l.Close()

	clilstner, err := net.Listen("tcp", ":0")
	if err != nil {
		log.Fatal("open punch client port error:", err)
		return
	}

	defer clilstner.Close()

	_, cliport, _ := net.SplitHostPort(clilstner.Addr().String())

	cliip, _, _ := net.SplitHostPort(ctrconn.RemoteAddr().String())
	openConns[port] = connection{uname, port, cliport, cliip, 0, 0}
	defer delete(openConns, port)

	for {
		conn, err := l.Accept()
		if err != nil {
			log.Fatal("accept extrernal connection error:", err)
		}

		nounce := randString(256)
		ctrconn.Write([]byte("CONNECT " + cliport + " " + nounce))

		connChn := make(chan net.Conn)
		errChn := make(chan error)

		// thread to wait for connection with correct nounce
		go func() {
			cliconn, err := clilstner.Accept()
			if err != nil {
				errChn <- fmt.Errorf("accept punch client connection error: %v", err)
				return
			}

			buf := make([]byte, 512)
			cliconn.Read(buf)
			clinounce := string(buf)

			if clinounce != nounce {
				errChn <- fmt.Errorf("Punch client connection fail, received: %v", clinounce)
				return
			}

			connChn <- cliconn
		}()

		// timeout after 10s
		select {
		case cliconn := <-connChn:
			go pipe(conn, cliconn)
		case err = <-errChn:
			fmt.Println(err)
			return
		case <-time.After(10 * time.Second):
			fmt.Println("Punch client connection timeout")
			return
		}
	}
}

func list() {

}

func verify(uname string, pw string, port string) bool {
	return false
}

func handleClient(conn net.Conn) {
	clistr := conn.RemoteAddr().String()
	fmt.Println("Received connection from " + clistr)

	buf := make([]byte, 512)
	nb, err := conn.Read(buf)

	if err != nil {
		fmt.Println("Lost connection from " + clistr)
		return
	}

	cmd := string(buf[0:nb])

	fmt.Println("Received command from " + clistr + ": " + cmd)

	tokens := optSplit.Split(cmd, -1)

	switch tokens[0] {
	case "OPEN":
		if len(tokens) != 4 {
			fmt.Println("Invalid OPEN operation from " + clistr)
			return
		}

		if !verify(tokens[1], tokens[2], tokens[3]) {
			conn.Write([]byte("FAIL"))
			conn.Close()
			return
		}

		openPort(conn, tokens[3], tokens[1])
	case "LIST":
		if len(tokens) != 1 {
			fmt.Println("Invalid OPEN operation from " + clistr)
			return
		}

		list()
	}
}

func main() {
	port := "9999"

	if len(os.Args) > 1 {
		port = os.Args[1]
	}

	l, err := net.Listen("tcp", ":"+port)

	if err != nil {
		log.Fatal("listen error:", err)
	}

	fmt.Println("Punch server is listening at port " + port)

	for {
		conn, err := l.Accept()
		if err != nil {
			log.Fatal("accept error:", err)
		}

		go handleClient(conn)
	}
}
