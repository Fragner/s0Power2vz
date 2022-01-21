#ifndef S0POWER2VZ_C
#define S0POWER2VZ_C
/**************************************************************************
S0/Impulse to Volkszaehler 'RaspberryPI deamon'.
converts the S0 impulse to powervalues
the updatecycle off the middleware is configurable in the config file by updateTime

https://github.com/fragner/s0Power2vz.git
Henrik Wellschmidt  <w3llschmidt@gmail.com>
changed by Martin Fragner <frama1038@gmail.com>
this is a fork from https://github.com/w3llschmid/s0vz.git

**************************************************************************/

/**************************************************************************

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

**************************************************************************/

#include <stdio.h>              /* standard library functions for file input and output */
#include <stdlib.h>             /* standard library for the C programming language, */
#include <string.h>             /* functions implementing operations on strings  */
#include <unistd.h>             /* provides access to the POSIX operating system API */
#include <sys/stat.h>           /* declares the stat() functions; umask */
#include <fcntl.h>              /* file descriptors */
#include <syslog.h>             /* send messages to the system logger */
#include <errno.h>              /* macros to report error conditions through error codes */
#include <signal.h>             /* signal processing */
#include <stddef.h>             /* defines the macros NULL and offsetof as well as the types ptrdiff_t, wchar_t, and size_t */

#include <libconfig.h>          /* reading, manipulating, and writing structured configuration files */
#include <curl/curl.h>          /* multiprotocol file transfer library */
#include <poll.h>			          /* wait for events on file descriptors */

#include <sys/ioctl.h>		/* */
#include <stdbool.h>
#include <wiringPi.h>
//
#include "constants.h"
#include "s0Power2vz.h"
#include "myConfig.h"

void daemonShutdown();
void signal_handler(int sig);

void daemonize(char *rundir, char *pidfile);
void writeCurlFailure2Log(const char *command );
void readCurlFailure2Log();
bool failure2LogExits();
char *getMyLine(FILE *f);

int i, len, running_handles, rc;
int m_updateTime = 60; //time setted by config-file
int m_debug = 1; //debug level setted by config-file
int vzport = 0;


//char gpio_pin_id[] = { 17, 18, 27, 22, 23, 24 }, url[128];
char gpio_pin_id[] = {2 }, url[256];

int inputs = sizeof(gpio_pin_id)/sizeof(gpio_pin_id[0]);

struct timeval tv;

CURL *easyhandle[sizeof(gpio_pin_id)/sizeof(gpio_pin_id[0])];
CURLM *multihandle;
CURLMcode multihandle_res;

static char errorBuffer[CURL_ERROR_SIZE+1];


/***********************************************
 * framas changes
*/
time_t m_tStart, m_tEnd;

static int cEMPTY = 99999999;
struct timeval m_tv2;
unsigned long long m_ullTStart;

FILE* devnull = NULL;

void update_curl_handle_value(const char *vzuuid, int iRun, int iVal);
bool checkTime(void);
int calcPower( int iImpCount);
unsigned long long unixtime_sec(void);

int m_once = 1;
bool bCurlFailure = false;
/***********************************************+*/
// globalCounter:
//	Global variable to count interrupts
//	Should be declared volatile to make sure the compiler doesn't cache it.

static volatile int globalCounter = 0 ;
/*
 * myInterrupt:
 *********************************************************************************
 */
void myInterrupt (void)
{
  ++globalCounter ;
}


void myPoll (void){
	struct pollfd fds[1];
	char buffer[BUF_LEN];
	int iImpCount=0;
	snprintf ( buffer, BUF_LEN, "/sys/class/gpio/gpio%d/value", gpio_pin_id[i] );
	if((fds[0].fd = open(buffer, O_RDONLY|O_NONBLOCK)) == 0) {
			syslog(LOG_INFO,"Error:%s (%m)", buffer);
			exit(1);
	}  
    fds[0].events = POLLPRI;
    fds[0].revents = 0;    
    int ret = poll(fds, inputs, 1000);
    if(ret>0) {
		if (fds[0].revents & POLLPRI) {
						len = read(fds[0].fd, buffer, BUF_LEN);
	//					update_curl_handle(vzuuid[i], i, cEMPTY);
						iImpCount++;
		}
	}
}

unsigned long long unixtime() {
	gettimeofday(&tv,NULL);
	unsigned long long ms_timestamp = (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;
    return ms_timestamp;
}

void update_curl_handle(const char *vzuuid, int iVal) {
  if (iVal == cEMPTY) {
    sprintf(url, "http://%s:%d/%s/data/%s.json?ts=%llu", vzserver, vzport, vzpath, vzuuid, unixtime());
  }
  else {
    sprintf(url, "http://%s:%d/%s/data/%s.json?ts=%llu&value=%d", vzserver, vzport, vzpath, vzuuid, unixtime(), iVal);
  }
  if (m_debug > 1) {                   
    syslog ( LOG_INFO, "send to this url:  %s", url );
  }
  
  CURLcode res;
  CURL *curl = curl_easy_init();
  if(curl) {    
    curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
	curl_easy_setopt(curl, CURLOPT_USERAGENT, DAEMON_NAME " " DAEMON_VERSION );
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, devnull);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    //curl_easy_setopt(curl, CURLOPT_URL, "http://192.168.24.120:80/volkszaehler/htdocs/middleware.php/data/fd3aec80-ed45-11e3-834a-11f2a80cada3.json?ts=1636135781457&value=60");
    res = curl_easy_perform(curl);
    /* Check for errors */
    if(res != CURLE_OK) {
      fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
	  writeCurlFailure2Log(url);
	  bCurlFailure = true;
    } else {
		//when it works again, send the missed values
		if (bCurlFailure) {
			readCurlFailure2Log();
			bCurlFailure = false;
		}
    	if (m_debug > 2 ) syslog(LOG_INFO, "curl sucessfully");  
    }              
    curl_easy_cleanup(curl);
  } else {
    syslog(LOG_PERROR, "ERROR curl not ready"); 
  }
  
  if (m_once > 0){
    syslog(LOG_INFO, "update_curl_handle: iVal = : %i",iVal);
  }
}
/*********************************************************************************
 * main
 *********************************************************************************/
int main(void) {

	if (wiringPiSetup () < 0)	{    
		syslog(LOG_WARNING,  "Unable to setup wiringPi: %s\n", strerror (errno));
		return 1 ;
	}
	if (wiringPiISR (BUTTON_PIN, INT_EDGE_FALLING, &myInterrupt) < 0) {
		syslog(LOG_WARNING, "Unable to setup ISR: %s\n", strerror (errno));
		return 1 ;
	}
	//init start time
	time(&m_tStart);
	m_ullTStart = unixtime_sec();
	//frama

	setlogmask(LOG_UPTO(LOG_INFO));
	openlog(DAEMON_NAME, LOG_CONS | LOG_PERROR, LOG_USER);
	syslog ( LOG_INFO, "S0-Impulse->Power to Volkszaehler RaspberryPI daemon %s.%s", DAEMON_VERSION, DAEMON_BUILD );
	cfile(inputs);
	char pid_file[22];
	sprintf ( pid_file, "/tmp/%s.pid", DAEMON_NAME );
	daemonize( "/tmp/", pid_file );

	freopen( "/dev/null", "r", stdin);
	freopen( "/dev/null", "w", stdout);
	freopen( "/dev/null", "w", stderr);
	devnull = fopen("/dev/null", "w+");

/*  
	curl_global_init(CURL_GLOBAL_ALL);
	multihandle = curl_multi_init(); 
  	easyhandle[0] = curl_easy_init();
	curl_easy_setopt(easyhandle[0], CURLOPT_URL, url);
	curl_easy_setopt(easyhandle[0], CURLOPT_POSTFIELDS, "");
	curl_easy_setopt(easyhandle[0], CURLOPT_USERAGENT, DAEMON_NAME " " DAEMON_VERSION );
	curl_easy_setopt(easyhandle[0], CURLOPT_WRITEDATA, devnull);
	curl_easy_setopt(easyhandle[0], CURLOPT_ERRORBUFFER, errorBuffer);
	curl_multi_add_handle(multihandle, easyhandle[0]);	
*/
	//check at start if file with missed data is available
 	if (failure2LogExits()){
		readCurlFailure2Log();	 
	}
	int myCounter = 0 ;
	int iImpCount = 0;
	int iTest = 0;
	int iPower = 0;
	int diff = 0;
	for ( ;; ) {	
		if (myCounter != globalCounter){
			diff = globalCounter - myCounter;
			myCounter = globalCounter;			
			if (diff != 1) {
				syslog(LOG_WARNING, " diff isn't 1 --> %d", diff); 
			}
			iImpCount++;
			if (m_debug > 2) {					
				syslog(LOG_INFO, "new impulse: %d", iImpCount);
			}
			iTest++;
			if (iTest >= 100) {
				iTest = 0;
				if (m_debug > 0) {					
					syslog(LOG_INFO, "%d signal's received, -> still alive ", iImpCount);//frama
				}
			}  
		}
    
		if ((m_updateTime > 0) && checkTime()) {
			iPower = calcPower(iImpCount);
			if (m_debug > 2) {
				syslog(LOG_INFO, "counted impulse: %d, calculated Power: %d", iImpCount, iPower);
			}
			update_curl_handle(vzuuid[0], iPower);
			//reset counter
			iImpCount = 0;
		}
    m_once = 0;//only first value's in syslog
    if (m_debug > 2){
      syslog(LOG_INFO, "waiting ...");      
    }
	    delay (100);
	}
	curl_global_cleanup();
  return 0;
}//main_end

/*****************************************
* framas functions
*/
/* powermeter has an resolution from 1000 Imp/kWh */
int calcPower( int iImpCount)	{
	int iPower = 0;
	if (m_once > 0){
		syslog(LOG_INFO, "calcPower: iImpCount = : %i, m_updateTime = %i", iImpCount, m_updateTime);
	}  
	if (iImpCount > 0) {
		iPower = (3600 * iImpCount)/ m_updateTime;
    if (iPower < 0) iPower = 0;//at startup a neg. value is calculated, why???
	}
	return iPower;
}
/*
	check the time difference between the calls
	when the diff is bigger than the time setted in the config file, trigger curl
	input: none
	return: true when the time diff is bigger than the configerd time
*/
bool checkTime (void) {
	int  iDiff;
	unsigned long long ullTEnd = unixtime_sec();
	iDiff = (int) (ullTEnd - m_ullTStart);
	if (m_debug > 3) {	
		;//syslog(LOG_INFO, "Startzeit: %llu, EndZeit: %llu, Differenz : %d",m_ullTStart, ullTEnd, iDiff);
	}
	if (iDiff >= m_updateTime) {
    if (m_debug > 2) {	
      syslog(LOG_INFO, "Es sind %d Sekunden vergangen, --> trigger Curl.", iDiff);
    }
		//init start time
		m_ullTStart = unixtime_sec();
		return true;
	}
	return false;
}

unsigned long long unixtime_sec() {
	gettimeofday(&m_tv2,NULL);
	return (unsigned long long)(m_tv2.tv_sec);
}

void writeCurlFailure2Log(const char *command ) {
  FILE *fptr;
  // opening file in append mode
  fptr = fopen("/var/log/s0Power2vzError.log", "a");
  if (fptr == NULL) {
      perror("Couldn't open File");
      exit(1);
  }
  fprintf(fptr, "%s\n", command);
  fclose(fptr);
}

/**
 * @brief read from failurelog and send data 
 * 
 */
void readCurlFailure2Log() {
	FILE *fptr;
	char *line = NULL;
	CURLcode res;
  	CURL *curl = curl_easy_init();
  	if(!curl) { 
		syslog(LOG_PERROR, "ERROR curl not ready");
		return;
  	}   
	//init curl paramters
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
	curl_easy_setopt(curl, CURLOPT_USERAGENT, DAEMON_NAME " " DAEMON_VERSION " Nachtrag" );
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, devnull);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);

	// opening file
	fptr = fopen("/var/log/s0Power2vzError.log", "r+");
	if (fptr == NULL) {
		perror("Couldn't open File");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_INFO,"send missing data via curl");
	
    do {        
        line = getMyLine(fptr);
        if (line != NULL) {
			//do something
			curl_easy_setopt(curl, CURLOPT_URL, line);	
			res = curl_easy_perform(curl);			
			/* Check for errors */
			if(res != CURLE_OK) {
				syslog(LOG_INFO, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));  
				fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			} else {
				if (m_debug > 2 ) syslog(LOG_INFO, "curl sucessfully %s", url);
			}		
			usleep(1000);
            free(line);
            line=NULL;
        } else {
            break;//leave endless loop
        }
    }while (true);
	curl_easy_cleanup(curl);  
	//clean up		
    fclose(fptr);
    if (line) free(line);
	ssize_t ret = rename("/var/log/s0Power2vzError.log", "/var/log/s0Power2vzError.done");	
	if(ret != 0) {
		syslog(LOG_INFO,"Error: unable to rename the file var/log/s0Power2vzError");
	}
}

bool failure2LogExits() {
	return (access("/var/log/s0Power2vzError.log", F_OK ) != -1);
}

/**
 * @brief Get the line object, without semicolons and linebreaks
 * 
 * @param f 
 * @return char* 
 */
char *getMyLine(FILE *f){
    int i=0, c=1 ;
    char *word = NULL;
    char character;
    const int maxLen = 255;
    if (c> 1) printf ("-->next run\n");
    word = malloc (sizeof(char) * maxLen);
    do {
        character = getc(f);
		//when getc get character > 127, the program crashes, why???
        if (character == EOF || (int)character > 127) {
           if (word) free(word);
            word = NULL;
            break;
        } 
        if (i > (maxLen*c)){
            printf ("word is too long, expand 'memory'\n");
            c++;
            word= realloc(word, sizeof(char) * maxLen * c );
        } 
        if (character != '\n'){
            word[i++] = character;
        } else {
            word[i++]='\0';
            break;
       }        
    } while (true);
    return word;
}
#endif