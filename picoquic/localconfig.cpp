
#include "localconfig.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <string.h>
#include <pthread.h>

// C Headers
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netdb.h>
#include <errno.h>

#include "configmessage.pb.h"

// XIA support
#include "xiaapi.hpp"
#include "dagaddr.hpp"

#define BUFSIZE 512

static std::string& ltrim(std::string& str, const std::string& chars= " \t\r")
{
    str.erase(0, str.find_first_not_of(chars));
    return str;
}
 
static std::string& rtrim(std::string& str, const std::string& chars= " \t\r")
{
    str.erase(str.find_last_not_of(chars) + 1);
    return str;
}
 
static std::string& trim(std::string& str, const std::string& chars= " \t\r")
{
    return ltrim(rtrim(str, chars), chars);
}

LocalConfig& LocalConfig::get_instance(const std::string& confFile)
{
	static LocalConfig config(confFile);
	return config;
}

LocalConfig::LocalConfig(const std::string& confFile)
{
	std::ifstream configuration(confFile);
	if(!configuration.is_open()) {
		throw "ERROR reading local config";
	}
	std::string line;
	while(std::getline(configuration, line)) {
		ltrim(line);
		if (line[0] == '#') {
			continue;
		}
		std::stringstream ss(line);
		std::string item;
		std::vector<std::string> entries;
		while(std::getline(ss, item, '=')) {
			entries.push_back(item);
		}
		if(entries.size() != 2) {
			throw "ERROR in config file";
		}
		_conf[trim(entries[0])] = trim(entries[1]);
	}
	configuration.close();
}

LocalConfig::LocalConfig(){
	this->control_socket = -1;
    this->aid = "";
    this->_name = "";
    this->_iface = "";
    this->_r_addr = "";
	this->_r_port = "";
	this->_r_ad = "";
	this->_r_hid = "";
	this->serverdag_str = "";
}

std::string LocalConfig::get(const std::string& param)
{
	auto it = _conf.find(param);
	if (it == _conf.end()) {
		return "";
	}
	return _conf[param];
}

void LocalConfig::stripInputLine(std::string& line)
{
	auto rm_whitespace = [](char ch) {
		return std::isspace<char>(ch, std::locale::classic());
	};

	line.erase(std::remove_if(line.begin(), line.end(), rm_whitespace),
			line.end());
}

int LocalConfig::configure(std::string control_port, std::string control_addr, 
            addr_info_t &raddr, addr_info_t &saddr)
{
   
 std::cout<<"Got "<<control_addr<<" : ";
 std::cout<<control_port<<std::endl;
 struct addrinfo hints, *res, *rp;
 int sock_fd;
 struct sockaddr_in addr;

  /* Create a socket file descriptor */
  if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
	printf("Failed to open listen socket\n");
	return -1;
  }

  int opt_value = 1;

  /* Eliminates "Address already in use" error from bind. */
  if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR,
				 (const void *)&opt_value , sizeof(int)) < 0) {
	printf("open_listen_socket: Failed to set \
    SO_REUSEADDR socket option\n");
	return -1;
  }

  bzero((char *)&addr, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(control_addr.c_str());
  addr.sin_port = htons(std::stoi(control_port.c_str()));

  if (bind(sock_fd, (struct sockaddr *)&addr,
		   sizeof(addr)) < 0) {
  	perror("\n");
	return -1;
  }

 if(listen(sock_fd, 1) < 0)
 {
 	printf("Failed while listening");
 	return -1;
 }

 // started listening on control socket
 this->control_socket = sock_fd;
 this->router_addr = &raddr;
 this->server_addr = &saddr;
 this->router_addr->addrlen = sizeof(sockaddr_x);
 this->loop = 0;


// Get the first configuration filled in conf
 void *ret = LocalConfig::config_controller((void *)this);
 if(ret)
   return -1;

 //this->controller_thread = 
 //std::thread controller_thr(&LocalConfig::config_controller, this, *this, 1);
 //std::thread controller_thr(&LocalConfig::test, 1);
 this->loop = 1;
 typedef void * (*THREADFUNCPTR)(void *);
 pthread_create(&this->control_thread, NULL, (THREADFUNCPTR)&LocalConfig::config_controller, (void *)this);

 return 0;
}


void *LocalConfig::config_controller(void *arg)
{
	LocalConfig *conf = (LocalConfig *)arg;
	std::cout<<"In config controller"<<std::endl;
	
	struct sockaddr_storage their_addr;
	socklen_t addr_size = sizeof(struct sockaddr);
	int new_fd;
	char buf[BUFSIZE];
	bzero(buf, BUFSIZE);
	
	do {
		
		// accept incoming configuration update requests
		new_fd = accept(conf->get_control_socket(), (struct sockaddr *)&their_addr, 
		&addr_size);
		if(new_fd < 0)
		{
			perror("\n");
			return (void *)-1;
		}
		int bytes_recvd = recv(new_fd, buf, BUFSIZE, 0);
		if(bytes_recvd < 0)
	 	{
	 		perror("\n");
	 		return (void *)-1;
	 	}
	 	// change this
	 	std::string s;
		int i=4;
	 	while(i<bytes_recvd)
	 		s.push_back(buf[i++]);
	 	std::cout<<"Length is "<<s.length()<<std::endl;
	 	configmessage::Config myconfig;
	 	myconfig.ParseFromString(s);
	 	// // //
	 	LocalConfig::update_serveraddr(*conf, myconfig.serverdag());
		LocalConfig::update_routeraddr(*conf, myconfig);
	 	
	}while(conf->loop);
	return NULL;
}

void LocalConfig::update_serveraddr(LocalConfig &conf, std::string serverdag)
{
	conf.set_serverdag_str(serverdag);
	std::cout<<"serverdag is "<<serverdag<<" len: "<<serverdag.length()<<std::endl;
	conf.server_addr->dag.reset(new Graph(serverdag));
	conf.server_addr->dag->fill_sockaddr(&conf.server_addr->addr);
}

void LocalConfig::update_routeraddr(LocalConfig &conf, configmessage::Config myconfig)
{
 	std::string router_addr = "RE " + myconfig.ad() + " " + myconfig.hid();
 	if(conf.get_our_addr().compare(router_addr) != 0 || conf.get_raddr().compare(myconfig.ipaddr()) != 0
 		|| conf.get_rport().compare(myconfig.port()) != 0)
 	{
 		std::string old_ad = conf->_r_ad;
 		std::string old_hid = conf->_r_hid;

 		conf._r_ad = myconfig.ad();
 		conf._r_hid = myconfig.hid();

		int sockfd = picoquic_xia_open_server_socket(conf.get_aid().c_str(), conf.router_addr->dag,
		myconfig.iface(), conf);
	 	if(sockfd < 0)
	 	{
	 		// restore
			conf._r_ad = old_ad;
			conf._r_hid = old_hid;
	 		return;
	 	}
	 	conf.router_addr->sockfd = sockfd;
	 	conf.router_addr->dag->fill_sockaddr(&conf.router_addr->addr);
	 	LocalConfig::set_config(conf, myconfig);
 	}
}


void LocalConfig::set_config(LocalConfig &conf, configmessage::Config myconfig)
{
	conf._name = myconfig.name();
	conf.aid = myconfig.aid();
	conf._r_addr = myconfig.ipaddr();
	conf._iface = myconfig.iface();
	conf._r_port = myconfig.port();
	// conf._r_ad = myconfig.ad();
	// conf._r_hid = myconfig.hid();
}

std::string LocalConfig::get_raddr()
{
	return this->_r_addr;
}

std::string LocalConfig::get_rport()
{
	return this->_r_port;
}

std::string LocalConfig::get_our_addr()
{
	return "RE " + this->_r_ad + " " + this->_r_hid;
}

std::string LocalConfig::get_their_addr()
{
	return "";
}

std::string LocalConfig::get_server_aid()
{
	return "";
}

std::string LocalConfig::get_router_iface()
{
	return this->_iface;
}

std::string LocalConfig::get_ticket_store()
{
	return "";
}

std::string LocalConfig::get_serverdag_str()
{
	return this->serverdag_str;
}

int LocalConfig::get_control_socket()
{
	return this->control_socket;
}

void LocalConfig::set_serverdag_str(std::string serverdag_str)
{
	if(this->serverdag_str.compare(serverdag_str) != 0)
		this->serverdag_str = serverdag_str;
}

std::string LocalConfig::get_aid()
{
	return this->aid;
}
