require 'rake'
require 'rake/clean'
require 'pathname'
require 'fileutils'


def pop_path(path)
  Pathname(path).each_filename.to_a[1..-1]
end

def obj2src(fn, e)
  File.join('src', pop_path(File.dirname(fn)), File.basename(fn).ext(e))
end

def is_windows?
  (/cygwin|mswin|mingw|bccwin|wince|emx/ =~ RUBY_PLATFORM) != nil
end

PROG = 'smoothie'

DEVICE = 'LPC1768'
ARCHITECTURE = 'armv7-m'

MBED_DIR = '/home/morris/Stuff/reprap/Firmwares/mbedgit/mbed/build/mbed'
MBED_OBJS = FileList["#{MBED_DIR}/TARGET_LPC1768/TOOLCHAIN_GCC_ARM/*.o"] #.exclude(/retarget/)

TOOLSBIN = './gcc-arm-none-eabi/bin/arm-none-eabi-'
CC = "#{TOOLSBIN}gcc"
CCPP = "#{TOOLSBIN}g++"
LD = "#{TOOLSBIN}g++"
OBJCOPY = "#{TOOLSBIN}objcopy"

SRC = FileList['src/**/*.{c,cpp}']

OBJDIR = 'OBJ'
OBJ = SRC.collect { |fn| File.join(OBJDIR, pop_path(File.dirname(fn)), File.basename(fn).ext('o')) } +
	["#{OBJDIR}/configdefault.o"]

# create destination directories
SRC.each do |s|
  d= File.join(OBJDIR, pop_path(File.dirname(s)))
  FileUtils.mkdir_p(d) unless Dir.exists?(d)
end

INCLUDE_DIRS = [Dir.glob(['./src/**/', './mri/**/'])].flatten
MBED_INCLUDE_DIRS = ["#{MBED_DIR}/", "#{MBED_DIR}/TARGET_LPC1768/"]

INCLUDE = (INCLUDE_DIRS+MBED_INCLUDE_DIRS).collect { |d| "-I#{d}" }.join(" ")

MRI_LIB = ' ./mri/mri.ar'
MBED_LIBS = " #{MBED_DIR}/TARGET_LPC1768/TOOLCHAIN_GCC_ARM/libmbed.a"
SYS_LIBS = ' -lstdc++_s -lsupc++_s -lm -lgcc -lc_s -lgcc -lc_s -lnosys'
LIBS = MBED_LIBS + SYS_LIBS + MRI_LIB

DEFINES = '-DCHECKSUM_USE_CPP -D__LPC17XX__  -DNO_TOOLS_TOUCHPROBE -DTARGET_LPC1768 ' +
	' -DMRI_ENABLE=0 -DMRI_INIT_PARAMETERS=0 -DMRI_BREAK_ON_INIT=0 -DMRI_SEMIHOST_STDIO=0' +
	' -DWRITE_BUFFER_DISABLE=0 -DSTACK_SIZE=3072 -DCHECKSUM_USE_CPP'

# Compiler flags used to enable creation of header dependencies.
#DEPFLAGS = -MMD -MP
CFLAGS = '-Wall -Wextra -Wno-unused-parameter -Wcast-align -Wpointer-arith -Wredundant-decls -Wcast-qual -Wcast-align -O2 -g3 -mcpu=cortex-m3 -mthumb -mthumb-interwork -ffunction-sections -fdata-sections  -fno-exceptions -fno-delete-null-pointer-checks'
CPPFLAGS = CFLAGS + ' -fno-rtti -std=gnu++11'

# Linker script to be used.  Indicates what code should be placed where in memory.
#LSCRIPT = "mbed/src/vendor/NXP/cmsis/LPC1768/GCC_ARM/LPC1768.ld"
LSCRIPT = "./LPC1768.ld"
LDFLAGS = "-mcpu=cortex-m3 -mthumb -specs=./build/startfile.spec" +
    " -Wl,-Map=#{OBJDIR}/smoothie.map,--cref,--gc-sections " +
    " -T#{LSCRIPT} -L #{MBED_DIR}/TARGET_LPC1768/TOOLCHAIN_GCC_ARM/"

# --wrap=_isatty,--wrap=malloc,--wrap=realloc,--wrap=free$(MRI_WRAPS)" +


CLEAN.include(OBJ)

task :default => [:build]

task :build => [:version, "#{PROG}.bin"]

task :version do
  if is_windows?
    VERSION = ' -D__GITVERSIONSTRING__=\"place-holder\"'
  else
    v1= `git symbolic-ref HEAD 2> /dev/null`
    v2= `git log --pretty=format:%h -1`
    VERSION = ' -D__GITVERSIONSTRING__=\"' + "#{v1[11..-1].chomp}-#{v2}".chomp + '\"'
    FileUtils.touch './src/version.cpp' # we want it compiled everytime
  end
end

file "#{OBJDIR}/configdefault.o" => 'src/config.default' do
  sh "cd ./src; ../#{OBJCOPY} -I binary -O elf32-littlearm -B arm --readonly-text --rename-section .data=.rodata.configdefault config.default ../#{OBJDIR}/configdefault.o"
end

file "#{PROG}.bin" => "#{PROG}.elf" do
  sh "#{OBJCOPY} -O binary #{OBJDIR}/#{PROG}.elf #{OBJDIR}/#{PROG}.bin"
end

file "#{PROG}.elf" => OBJ do
  sh "#{LD} #{LDFLAGS} #{OBJ} #{MBED_OBJS.join(' ')} #{LIBS}  -o #{OBJDIR}/#{PROG}.elf"
end

rule '.o' => lambda{ |objfile| obj2src(objfile, 'cpp') } do |t|
  sh "#{CCPP} #{CPPFLAGS} #{INCLUDE} #{DEFINES} #{VERSION} -c -o #{t.name} #{t.source}"
end

rule '.o' => lambda{ |objfile| obj2src(objfile, 'c') } do |t|
  sh "#{CC} #{CFLAGS} #{INCLUDE} #{DEFINES} #{VERSION} -c -o #{t.name} #{t.source}"
end

