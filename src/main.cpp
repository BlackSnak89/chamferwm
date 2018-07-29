#include "main.h"
#include "container.h"
#include "backend.h"
#include "compositor.h"

#include <cstdlib>
#include <stdarg.h>
#include <time.h>

#include <args.hxx>
#include <iostream>

#include <sys/epoll.h>
#include <signal.h>
#include <sys/signalfd.h>

Exception::Exception(){
	this->pmsg = buffer;
}

Exception::Exception(const char *pmsg){
	this->pmsg = pmsg;
}

Exception::~Exception(){
	//
}

const char * Exception::what(){
	return pmsg;
}

char Exception::buffer[4096];

void DebugPrintf(FILE *pf, const char *pfmt, ...){
	time_t rt;
	time(&rt);
	const struct tm *pti = localtime(&rt);

	char tbuf[256];
	strftime(tbuf,sizeof(tbuf),"[xwm %F %T]",pti);
	fprintf(pf,"%s ",tbuf);

	va_list args;
	va_start(args,pfmt);
	//pf = fopen("/tmp/log1","a+");
	if(pf == stderr)
		fprintf(pf,"Error: ");
	vfprintf(pf,pfmt,args);
	//fclose(pf);
	va_end(args);
}

class RunBackend{
public:
	RunBackend() : pcomp(0){}
	virtual ~RunBackend(){}
	void SetCompositor(class RunCompositor *pcomp){
		this->pcomp = pcomp;
	}
protected:
	class RunCompositor *pcomp;
};

class RunCompositor{
public:
	RunCompositor(){}
	virtual ~RunCompositor(){}
	virtual void Present() = 0;
};

class DefaultBackend : public Backend::Default, public RunBackend{
public:
	DefaultBackend() : Default(), RunBackend(){
		Start();
		DebugPrintf(stdout,"Backend initialized.\n");
	}
	
	~DefaultBackend(){}

	void DefineBindings(){
		DebugPrintf(stdout,"DefineKeybindings()\n");
	}

	Backend::X11Client * SetupClient(const Backend::X11Client::CreateInfo *pcreateInfo){
		Compositor::X11Compositor *pcomp11 = dynamic_cast<Compositor::X11Compositor *>(pcomp);
		if(!pcomp11)
			return new Backend::X11Client(pcreateInfo);
		Compositor::X11ClientFrame *pclientFrame = new Compositor::X11ClientFrame(pcreateInfo);
		return pclientFrame;
	}

	void EventNotify(const Backend::BackendEvent *pevent){
		Compositor::X11Compositor *pcomp11 = dynamic_cast<Compositor::X11Compositor *>(pcomp);
		if(!pcomp11)
			return;
		const Backend::X11Event *pevent11 = dynamic_cast<const Backend::X11Event *>(pevent);
		pcomp11->FilterEvent(pevent11);
	}
};

class DebugBackend : public Backend::Debug, public RunBackend{
public:
	DebugBackend() : Debug(), RunBackend(){
		Start();
		DebugPrintf(stdout,"Backend initialized.\n");
	}

	~DebugBackend(){}

	Backend::DebugClient * SetupClient(const Backend::DebugClient::CreateInfo *pcreateInfo){
		return 0;
	}

	void DefineBindings(){
		DebugPrintf(stdout,"DefineKeybindings()\n");
	}

	void EventNotify(const Backend::BackendEvent *pevent){
		//
	}
};

class DefaultCompositor : public Compositor::X11Compositor, public RunCompositor{
public:
	DefaultCompositor(uint gpuIndex, WManager::Container *proot, Backend::X11Backend *pbackend) : X11Compositor(gpuIndex,pbackend), RunCompositor(){
		Start();
		GenerateCommandBuffers(proot); //TODO: move to present
		DebugPrintf(stdout,"Compositor enabled.\n");
	}

	~DefaultCompositor(){
		Stop();
	}

	void Present(){
		Compositor::X11Compositor::Present();
	}
};

class DebugCompositor : public Compositor::X11DebugCompositor, public RunCompositor{
public:
	DebugCompositor(uint gpuIndex, WManager::Container *proot, Backend::X11Backend *pbackend) : X11DebugCompositor(gpuIndex,pbackend), RunCompositor(){
		Compositor::X11DebugCompositor::Start();
		GenerateCommandBuffers(proot);
		DebugPrintf(stdout,"Compositor enabled.\n");
	}

	~DebugCompositor(){
		Compositor::X11DebugCompositor::Stop();
	}

	void Present(){
		Compositor::X11DebugCompositor::Present();
	}
};

class NullCompositor : public Compositor::NullCompositor, public RunCompositor{
public:
	NullCompositor() : Compositor::NullCompositor(), RunCompositor(){
		Start();
	}

	~NullCompositor(){
		Stop();
	}

	void Present(){
		//
	}
};

int main(sint argc, const char **pargv){	
	args::ArgumentParser parser("xwm - A compositing window manager","");
	args::HelpFlag help(parser,"help","Display this help menu",{'h',"help"});

	args::Group group_backend(parser,"Backend",args::Group::Validators::DontCare);
	args::Flag debugBackend(group_backend,"debugBackend","Create a test environment for the compositor engine without redirection. The application will not as a window manager.",{'d',"debug-backend"});

	args::Group group_comp(parser,"Compositor",args::Group::Validators::DontCare);
	args::Flag noComp(group_comp,"noComp","Disable compositor.",{"no-compositor",'n'});
	args::ValueFlag<uint> gpuIndex(group_comp,"id","GPU to use by its index. By default the first device in the list of enumerated GPUs will be used.",{"device-index"},0);
	//args::ValueFlag<std::string> shaderPath(group_comp,"path","Path to SPIR-V shader binary blobs",{"shader-path"},".");

	try{
		parser.ParseCLI(argc,pargv);

	}catch(args::Help){
		std::cout<<parser;
		return 0;

	}catch(args::ParseError e){
		std::cout<<e.what()<<std::endl<<parser;
		return 1;
	}

#define MAX_EVENTS 1024
	struct epoll_event event1, events[MAX_EVENTS];
	sint efd = epoll_create1(0);
	if(efd == -1){
		DebugPrintf(stderr,"epoll efd\n");
		return 1;
	}

	//https://stackoverflow.com/questions/43212106/handle-signals-with-epoll-wait
	/*signal(SIGINT,SIG_IGN);

	sigset_t signals;
	sigemptyset(&signals);
	sigaddset(&signals,SIGINT);
	sint sfd = signalfd(-1,&signals,SFD_NONBLOCK);
	if(sfd == -1){
		DebugPrintf(stderr,"signal fd\n");
		return 1;
	}
	sigprocmask(SIG_BLOCK,&signals,0);
	event1.data.ptr = &sfd;
	event1.events = EPOLLIN;
	epoll_ctl(sfd,EPOLL_CTL_ADD,sfd,&event1);*/

	WManager::Container *proot = new WManager::Container();
	WManager::Container *pna = new WManager::Container(proot); //temp: testing
	pna->pclient = new Backend::DebugClient(400,10,400,200);
	WManager::Container *pnb = new WManager::Container(proot); //temp: testing
	pnb->pclient = new Backend::DebugClient(10,10,400,800);

	RunBackend *pbackend;
	try{
		if(debugBackend.Get())
			pbackend = new DebugBackend();
		else pbackend = new DefaultBackend();
	}catch(Exception e){
		DebugPrintf(stderr,"%s\n",e.what());
		return 1;
	}

	Backend::X11Backend *pbackend11 = dynamic_cast<Backend::X11Backend *>(pbackend);
	sint fd = pbackend11->GetEventFileDescriptor();
	if(fd == -1){
		DebugPrintf(stderr,"XCB fd\n");
		return 1;
	}
	event1.data.ptr = &fd;
	event1.events = EPOLLIN;
	epoll_ctl(efd,EPOLL_CTL_ADD,fd,&event1);

	RunCompositor *pcomp;
	try{
		if(noComp.Get())
			pcomp = new NullCompositor();
		else
		if(debugBackend.Get())
			pcomp = new DebugCompositor(gpuIndex.Get(),proot,pbackend11);
		else pcomp = new DefaultCompositor(gpuIndex.Get(),proot,pbackend11);
	}catch(Exception e){
		DebugPrintf(stderr,"%s\n",e.what());
		delete pbackend;
		return 1;
	}

	pbackend->SetCompositor(pcomp);

	//https://github.com/karulont/i3lock-blur/blob/master/xcb.c
	for(bool r = true; r;){
		//for(sint n = epoll_wait(efd,events,MAX_EVENTS,-1), i = 0; i < n; ++i){
		//for(sint n = epoll_wait(efd,events,MAX_EVENTS,0), i = 0; i < n; ++i){
		sint n = epoll_wait(efd,events,MAX_EVENTS,0);
		for(sint i = 0; i < n; ++i){
			if(events[i].data.ptr == &fd){
				r = pbackend11->HandleEvent();
				if(!r)
					break;
			}
			/*if(!q){
			q = true;
			try{
				pcomp->Present();

			}catch(Exception e){
				DebugPrintf(stderr,"%s\n",e.what());
				break;
			}}*/
		}
		try{
			pcomp->Present();

		}catch(Exception e){
			DebugPrintf(stderr,"%s\n",e.what());
			break;
		}
	}

	DebugPrintf(stdout,"Exit\n");

	delete pcomp;
	delete pbackend;
	delete proot;

	return 0;
}

