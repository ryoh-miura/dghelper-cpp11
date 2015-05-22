#include <type_traits>
#include <typeinfo>
#include <memory>
#include <initializer_list>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>

#include <cxxabi.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <execinfo.h>

using namespace std;

template <class T>
string
type_name() {
  unique_ptr<char, void(*)(void*)> own (
              abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, nullptr),
              ::free);
  return own != nullptr ? own.get() : typeid(T).name();
}

string
to_demangle_name(const char * funcname) {
  unique_ptr<char, void(*)(void*)> own (
              abi::__cxa_demangle(funcname, nullptr, nullptr, nullptr), ::free);
  return own != nullptr ? own.get() : string("");
}

vector< string >
split(string & s, const string delims) {
  vector<string> ret;
  string::size_type idx=0, found = string::npos;
  while (true) {
    found = s.find_first_of(delims, idx);
    if (idx == found) { idx++; continue; }
    if (found == string::npos || idx > s.size() ) break;
    ret.push_back( s.substr(idx, found - idx) );
    idx = found + 1;
  }
  if ( found == string::npos && idx < s.size())
    ret.push_back( s.substr(idx) );
  return ret;
}

class Backtrace {
private:
  typedef tuple<int, string, uint64_t, string, int> frame_info_type;
  typedef vector< frame_info_type> bt_info_type;
  bt_info_type info_;
  int size_;  
public:
  Backtrace(const int nframe=100) :
    info_(),  size_(-1) {
    void ** addr = static_cast<void**> ( alloca(sizeof(void*) * nframe) );
    int size = backtrace(addr, nframe);
    if (size == nframe) {
      fprintf(stderr, "Check the backtrace's frame size\n");
      return;
    }
    char ** symbols = backtrace_symbols(addr, nframe);
    if (NULL == symbols) {
      fprintf(stderr, "Cannot get the backtrace's symbols\n");
      return;
    }
    unique_ptr<char*, function<void(void*) > >
      symptr( symbols,
	      [](void * addr) { free(addr); }  );
    vector<string> lines(symptr.get(), symptr.get() + nframe);
    int i=0;
    for (auto line : lines) {
      // a sample line format:
      // 1         a.out          0x0000000104b5bf08 _Z8hogehogev + 24
      // lineno    filename       address             funcname      func-offset      
      vector<string> elems  = split(line, " ");
      string funcname(elems[3]);
      const char * name = elems[3].c_str();

      int status;
      char * demangled = abi::__cxa_demangle(name, 0, 0, &status);
      if (NULL != demangled) {
	funcname = demangled;
	free(demangled);
      }
      // Did the following const char ptr need ...? 
      const char * lineno = elems[0].c_str();
      const char * addr = elems[2].c_str();
      const char * offset = elems[5].c_str();
      info_.push_back(
		      make_tuple(
				 strtol(lineno, NULL, 10),  
				 elems[1],
				 static_cast<uint64_t>(strtoll( addr, NULL, 16) ),
				 funcname,
				 strtol(offset, NULL, 10)
				 )
		      );
      i++;
      if (i>=size-1) break;
    }
    size_ = size;
  }
  bt_info_type & get_frames() {    return info_;  }
  int get_frame_size() { return size_; }
  void dump_frames() {
    for (int i=0, len=info_.size(); i<len; ++i) {
      frame_info_type & f = info_[i];
      printf("%d %s %016llX %s  %d\n",
	     get<0>(f), get<1>(f).c_str(), get<2>(f), get<3>(f).c_str(), get<4>(f) );
    }
  }
  uint64_t
  caller_frame_addr(int up=0, string * func = nullptr) {
    if (size_<=up+1) abort();
    frame_info_type & f = info_[up];
    uint64_t caller_loc = get<2>(f);
    if (nullptr != func)
      *func = string(get<3>(f) );
    int offset = get<4>(f);
    return caller_loc - offset;
  }
  frame_info_type
  caller_frame(int up=0) {
    if (size_ <= up + 1) abort();
    return info_[up];
  }
};

class Caller {
public:
  Caller(const char * fname, const int line, const char * pfunc, bool v=true) :
    fname_(fname), line_(line), pfunc_(pfunc), enter_("*** Enter"), leave_("*** Leave"), nspace_(0)  {
    Backtrace bt(100);
    verbose_ = v;
    std::string s;
    const int magic_nframe = 4, dels = 3;
    uint64_t addr = bt.caller_frame_addr(magic_nframe, &s);
    const int sz = bt.get_frame_size() - magic_nframe - dels;
    nspace_ = sz;    
    string space(nspace_, '+');
    if (verbose_)
      printf("%s%s/[%s:%04d]: %s\n", space.c_str() , enter_, fname_.c_str() , line_, pfunc_.c_str());
    printf("%s%s%s 0x%016llX\n", space.c_str() , "--- ", s.c_str(), addr);
  }
  ~Caller() {
    if (verbose_) {
      string space(nspace_, '+');      
      printf("%s%s/[%s:%04d]: %s\n", space.c_str(), leave_, fname_.c_str(), line_, pfunc_.c_str());
    }
  }
private:  
  std::string fname_;
  int line_;
  std::string pfunc_;
  const char * enter_; //  = "*** Enter";
  const char * leave_; //  = "*** Leave";
  bool verbose_;
  int nspace_;
#if 0
# define  CALLER_EMBEDED     __FILE__, __LINE__, __PRETTY_FUNCTION__, false
#else
# define  CALLER_EMBEDED     __FILE__, __LINE__, __PRETTY_FUNCTION__
#endif
};

#if 1
void func(void) {
  Caller c(CALLER_EMBEDED);
}
template <typename a, typename b, typename c> struct cba{
public:
  cba() {   Caller __o(CALLER_EMBEDED); }
};

class abc {};
class def : public abc {
public:
  def() {
    Caller __o(CALLER_EMBEDED);
    one();
  }
  void one() {
    Caller __o(CALLER_EMBEDED);
    Backtrace bt; bt.dump_frames();
  }
};

class Base {
public:
   virtual void vvfunc() {}
};

class Derived : public Base {};

int main() {
  Caller __o(CALLER_EMBEDED);  
  cba<int, cba<double, int, int>, double > b;
  func();
  for (int i=0; i<3; ++i)
    new def();

  Derived* pd = new Derived;
  Base* pb = pd;
  cout << to_demangle_name( typeid( pb ).name() )  << endl;   //prints " Base *"
  cout << to_demangle_name( typeid( *pb ).name() ) << endl;   //prints " Derived"
  cout << to_demangle_name( typeid( pd ).name() ) << endl;    //prints " Derived *"
  cout << to_demangle_name( typeid( *pd ).name() ) << endl;   //prints " Derived"

  cout << type_name<decltype(pb)>()  << endl;   //prints "Base *"
  cout << type_name<decltype(*pb)>() << endl;   //prints "Base"
  cout << type_name<decltype(pd )>() << endl;   //prints "Derived *"
  cout << type_name<decltype(*pd)>()  << endl;  //prints "Derived"

  delete pd;

  return 0;
}
#endif  // __MAIN__

#if 0
int main() {
string s("7   libdyld.dylib                       0x00007fff988fe5c9 start + 1");
vector<string> s2 = split(s, " ");
for (auto & x : s2) {
  printf("%s\n", x.c_str() );
} 
return 0;
}
#endif

#if 0
Local Variables:
quickrun-option-cmd-alist: ((:command . "clang++")
			    (:exec    . ("%c -std=c++11 -o %n %s -Wall -Wextra -D__MAIN__"
					 "%n "))
                            (:remove  . ("%n")))
  End:
#endif
