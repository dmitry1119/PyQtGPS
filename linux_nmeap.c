/*
Copyright (c) 2005, David M Howard (daveh at dmh2000.com)
All rights reserved.

This product is licensed for use and distribution under the BSD Open Source License.
see the file COPYING for more details.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, 
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; 
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE 
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, 
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 

*/

/*
========================================================================================================
EXAMPLE : SETUP FOR GGA AND RMC SENTENCES WITH LINUX SERIAL IO
=======================================================================================================
*/   


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/io.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>
#include <errno.h>

#include "nmeap.h"

nmeap_gga_t g_gga;

/** do something with the GGA data */
static void print_gga(nmeap_gga_t *gga)
{
    printf("found GPGGA message %.6f %.6f %.0f %lu %d %d %f %f\n",
            gga->latitude  ,
            gga->longitude, 
            gga->altitude , 
            gga->time     , 
            gga->satellites,
            gga->quality   ,
            gga->hdop      ,
            gga->geoid     
            );
}

/** called when a gpgga message is received and parsed */
static void gpgga_callout(nmeap_context_t *context,void *data,void *user_data)
{
    nmeap_gga_t *gga = (nmeap_gga_t *)data;
    
    printf("-------------callout\n");
    print_gga(gga);
}


/** do something with the RMC data */
static void print_rmc(nmeap_rmc_t *rmc)
{
    printf("found GPRMC Message %lu %c %.6f %.6f %f %f %lu %f\n",
            rmc->time,
            rmc->warn,
            rmc->latitude,
            rmc->longitude,
            rmc->speed,
            rmc->course,
            rmc->date,
            rmc->magvar
            );
}

/** called when a gprmc message is received and parsed */
static void gprmc_callout(nmeap_context_t *context,void *data,void *user_data)
{
    nmeap_rmc_t *rmc = (nmeap_rmc_t *)data;
    
    printf("-------------callout\n");
    print_rmc(rmc);
}


/*************************************************************************
 * LINUX IO
 */
 
/**
 * open the specified serial port for read/write
 * @return port file descriptor or -1
 */
int openPort(const char *tty,int baud)
{
	int     status;
	int     fd;
	struct termios newtio;
    
    // open the tty
    fd = open(tty,O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("open");
        return fd;
    }

    // flush serial port
    status = tcflush(fd, TCIFLUSH);
    if (status < 0) {
        perror("tcflush");
        close(fd);
        return -1;
    }
    
    /* get current terminal state */
    tcgetattr(fd,&newtio);

    // set to raw terminal type
    newtio.c_cflag = baud | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNBRK | IGNPAR;
    newtio.c_oflag = 0;    
    
    // control parameters
    newtio.c_cc[VMIN]     = 1;   // block for at least one charater 
    
    // set its new attrigutes
    status = tcsetattr(fd,TCSANOW,&newtio);
    if (status < 0) {
        perror("tcsetattr");
        close(fd);
        fd = -1;
        return fd;
    }

	return fd;
}


/* ---------------------------------------------------------------------------------------*/
/* STEP 1 : allocate the data structures. be careful if you put them on the stack because */
/*          they need to be live for the duration of the parser                           */
/* ---------------------------------------------------------------------------------------*/
static nmeap_context_t nmea;	   /* parser context */
static nmeap_gga_t     gga;		   /* this is where the data from GGA messages will show up */
static nmeap_rmc_t     rmc;		   /* this is where the data from RMC messages will show up */
static int             user_data; /* user can pass in anything. typically it will be a pointer to some user data */

int main(int argc,char *argv[])
{
    int  status;
    int  rem;
	int  offset;
	int  len;
	char buffer[32];
    int  fd;
    const char *port = "/dev/ttyS0";
    
    // default to ttyS0 or invoke with 'linux_nmeap <other serial device>' 
    if (argc == 2) {
        port = argv[1];
    }
    
    /* --------------------------------------- */
    /* open the serial port device             */
    /* using default 4800 baud for most GPS    */
    /* --------------------------------------- */
    fd = openPort(port,B4800);
    if (fd < 0) {
        /* open failed */
        printf("openPort %d\n",fd);
        return fd;
    }
    
	/* ---------------------------------------*/
	/*STEP 2 : initialize the nmea context    */                                                
	/* ---------------------------------------*/
    status = nmeap_init(&nmea,(void *)&user_data);
    if (status != 0) {
        printf("nmeap_init %d\n",status);
        exit(1);
    }
    
	/* ---------------------------------------*/
	/*STEP 3 : add standard GPGGA parser      */                                                
	/* -------------------------------------- */
    status = nmeap_addParser(&nmea,"GPGGA",nmeap_gpgga,gpgga_callout,&gga);
    if (status != 0) {
        printf("nmeap_add %d\n",status);
        exit(1);
    }

	/* ---------------------------------------*/
	/*STEP 4 : add standard GPRMC parser      */                                                
	/* -------------------------------------- */
    status = nmeap_addParser(&nmea,"GPRMC",nmeap_gprmc,gprmc_callout,&rmc);
    if (status != 0) {
        printf("nmeap_add %d\n",status);
        exit(1);
    }
    
	/* ---------------------------------------*/
	/*STEP 5 : process input until done       */                                                
	/* -------------------------------------- */
    for(;;) {
		/* ---------------------------------------*/
		/*STEP 6 : get a buffer of input          */                                                
		/* -------------------------------------- */
        len = rem = read(fd,buffer,sizeof(buffer));
        if (len <= 0) {
            perror("read");
            break;
        }
        
        
		/* ----------------------------------------------*/
		/*STEP 7 : process input until buffer is used up */                                                
		/* --------------------------------------------- */
		offset = 0;
        while(rem > 0) {
			/* --------------------------------------- */
			/*STEP 8 : pass it to the parser           */
			/* status indicates whether a complete msg */
			/* arrived for this byte                   */
			/* NOTE : in addition to the return status */
			/* the message callout will be fired when  */
			/* a complete message is processed         */
			/* --------------------------------------- */
            status = nmeap_parseBuffer(&nmea,&buffer[offset],&rem);
			offset += (len - rem); 
            
			/* ---------------------------------------*/
			/*STEP 9 : process the return code        */
            /* DON"T NEED THIS IF USING CALLOUTS      */
            /* PICK ONE OR THE OTHER                  */
			/* -------------------------------------- */
            switch(status) {
            case NMEAP_GPGGA:
                printf("-------------switch\n");
                print_gga(&gga);
                printf("-------------\n");
                break;
            case NMEAP_GPRMC:
                printf("-------------switch\n");
                print_rmc(&rmc);
                printf("-------------\n");
                break;
            default:
                break;
            }
        }
    }
    
    /* close the serial port */
    close(fd);
    
    return 0;
}

