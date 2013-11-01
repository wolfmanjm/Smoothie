#include "sftpd.h"
#include "string.h"
#include "stdlib.h"

extern "C" {
#include "uip.h"
}

#define ISO_nl 0x0a
#define ISO_cr 0x0d
#define ISO_sp 0x20


Sftpd::Sftpd()
{
    fd= NULL;
    state= STATE_NORMAL;
    outbuf= NULL;
}

Sftpd::~Sftpd()
{
    if(fd != NULL) {
        fclose(fd);
    }
}

int Sftpd::senddata()
{
    PSOCK_BEGIN(&sout);
    if(outbuf != NULL) {
        PSOCK_SEND(&sout, outbuf, strlen(outbuf)+1);
    }
    outbuf= NULL;
    PSOCK_END(&sout);
}

int Sftpd::newdata()
{
    PSOCK_BEGIN(&sin);
    while(1) {
        if(state == STATE_CONNECTED) {
            PSOCK_READTO(&sin, ISO_nl);
            buf[PSOCK_DATALEN(&sin)-1]= 0;
            printf("sftp: got command: %s\n", buf);
            if(strncmp(buf, "USER", 4) == 0) {
                outbuf= "!user logged in";
            }else if(strncmp(buf, "DONE", 4) == 0) {
                outbuf= "+ exit";
                state= STATE_CLOSE;
                break;

            }else if(strncmp(buf, "STOR", 4) == 0) {
                int len= PSOCK_DATALEN(&sin)-1;
                if(len < 11) {
                    outbuf= "- incomplete STOR command";
                }else {
                    char *fn= &buf[9];
                    // get { NEW|OLD|APP }
                    if(strncmp(&buf[5], "OLD", 3) == 0) {
                        printf("sftp: Opening file: %s\n", fn);
                        fd= fopen(fn, "w");
                        if(fd != NULL) {
                            outbuf= "+ new file";
                            state= STATE_GET_LENGTH;
                        }else{
                            outbuf= "- failed";
                        }
                    }else if(strncmp(&buf[5], "APP", 3) == 0) {
                        fd= fopen(fn, "a");
                        if(fd != NULL) {
                            outbuf= "+ append file";
                            state= STATE_GET_LENGTH;
                        }else{
                            outbuf= "- failed";
                        }
                    }else {
                        outbuf= "- Only OLD|APP supported";
                    }
                }
            }else{
                outbuf= "- Unknown command";
            }

        }else if(state == STATE_GET_LENGTH) {
            PSOCK_READTO(&sin, ISO_nl);
            if(strncmp(buf, "SIZE", 4) != 0) {
                fclose(fd);
                fd= NULL;
                outbuf= "- Expected size";
                state= STATE_CONNECTED;
            }else{
                filesize= atoi(&buf[5]);
                if(filesize > 0){
                    outbuf= "+ ok, waiting for file";
                    state= STATE_DOWNLOAD;
                    PSOCK_EXIT(&sin); // we need to yield because we are switching to direct access bypassing PSOCK
                }else{
                    fclose(fd);
                    fd= NULL;
                    outbuf= "- bad filesize";
                    state= STATE_CONNECTED;
                }
            }

        }else if(state == STATE_DOWNLOAD) {
            // Note this is not using PSOCK
            printf("sftp: starting download, expecting %d bytes\n", filesize);
            char *readptr = (char *)uip_appdata;
            int readlen = uip_datalen();
            if(filesize > 0 && readlen > 0) {
                if(readlen > filesize) readlen= filesize;
                if(fwrite(readptr, 1, readlen, fd) != readlen) {
                    printf("sftp: Error writing file\n");
                    fclose(fd);
                    fd= NULL;
                    outbuf= "- Error saving file";
                    state= STATE_CONNECTED;
                    break;
                }
                filesize -= readlen;
                printf("sftp: saved %d bytes %d left\n", readlen, filesize);
            }
            if(filesize == 0) {
                printf("sftp: download complete\n");
                fclose(fd);
                fd= NULL;
                outbuf= "+ Saved file";
                state= STATE_CONNECTED;
            }
            break; // we need to yield back

        }else{
            printf("sftp: Not in recognized state: %d\n", state);
            state= STATE_CLOSE;
            break;
        }
    }
    PSOCK_END(&sin);
}

int Sftpd::acked()
{
    return 0;
}


void Sftpd::appcall(void)
{
    if (uip_connected()) {
        PSOCK_INIT(&sin, buf, sizeof(buf));
        PSOCK_INIT(&sout,buf, sizeof(buf));
        state = STATE_CONNECTED;
        outbuf= "+Smoothie SFTP Service";
    }

    if (state == STATE_CLOSE) {
        printf("sftp: state close\n");
        state = STATE_NORMAL;
        uip_close();
        return;
    }

    if (uip_closed() || uip_aborted() || uip_timedout()) {
        printf("sftp: closed\n");
        if(fd != NULL)
            fclose(fd);
        fd= NULL;
        return;
    }

    // if (uip_acked()) {
    //     printf("sftp: acked\n");
    //     this->acked();
    // }

    if (uip_newdata()) {
        printf("sftp: newdata\n");
        this->newdata();
    }


    if (uip_rexmit() || uip_newdata() || uip_acked() || uip_connected() || uip_poll()) {
        this->senddata();
    }

    if(uip_poll() && uip_stopped(uip_conn)) {
        printf("sftp: restart\n");
        uip_restart();
    }
}

void Sftpd::init(void)
{

}


