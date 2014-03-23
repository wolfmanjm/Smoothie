require 'rake'
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

MBED_DIR = './mbed/drop'

TOOLSBIN = './gcc-arm-none-eabi/bin/arm-none-eabi-'
CC = "#{TOOLSBIN}gcc"
CCPP = "#{TOOLSBIN}g++"
LD = "#{TOOLSBIN}g++"
OBJCOPY = "#{TOOLSBIN}objcopy"
SIZE = "#{TOOLSBIN}size"

# set to true to eliminate all the network code
NONETWORK= false
# list of modules to exclude, include directory it is in
EXCLUDE_MODULES= %w(tools/touchprobe)
# e.g for a CNC machine
#EXCLUDE_MODULES = %w(tools/touchprobe tools/laser tools/temperaturecontrol tools/extruder)

# generate regex of modules to exclude and defines
exclude_defines, excludes = EXCLUDE_MODULES.collect { |e|  [e.tr('/', '_').upcase, e.sub('/', '\/')] }.transpose

# see if network is enabled
if ENV['NONETWORK'] || NONETWORK
  nonetwork= true
  excludes << '\/libs\/Network\/'
  puts "Excluding Network code"
else
  nonetwork= false
end

SRC = FileList['src/**/*.{c,cpp}'].exclude(/#{excludes.join('|')}/)

puts "WARNING Excluding modules: #{EXCLUDE_MODULES.join(' ')}" unless exclude_defines.empty?

OBJDIR = 'OBJ'
OBJ = SRC.collect { |fn| File.join(OBJDIR, pop_path(File.dirname(fn)), File.basename(fn).ext('o')) } +
	%W(#{OBJDIR}/configdefault.o #{OBJDIR}/mbed_custom.o)

# create destination directories
SRC.each do |s|
  d= File.join(OBJDIR, pop_path(File.dirname(s)))
  FileUtils.mkdir_p(d) unless Dir.exists?(d)
end

INCLUDE_DIRS = [Dir.glob(['./src/**/', './mri/**/'])].flatten
MBED_INCLUDE_DIRS = %W(#{MBED_DIR}/ #{MBED_DIR}/LPC1768/)

INCLUDE = (INCLUDE_DIRS+MBED_INCLUDE_DIRS).collect { |d| "-I#{d}" }.join(" ")

MRI_ENABLE = true # set to false to disable MRI
MRI_LIB = MRI_ENABLE ? './mri/mri.ar' : ''
MBED_LIB = "#{MBED_DIR}/LPC1768/GCC_ARM/libmbed.a"
SYS_LIBS = '-lstdc++_s -lsupc++_s -lm -lgcc -lc_s -lgcc -lc_s -lnosys'
LIBS = [MBED_LIB, SYS_LIBS, MRI_LIB].join(' ')

MRI_DEFINES = MRI_ENABLE ? %w(-DMRI_ENABLE=1 -DMRI_INIT_PARAMETERS='"MRI_UART_0 MRI_UART_SHARE"' -DMRI_BREAK_ON_INIT=0 -DMRI_SEMIHOST_STDIO=0) : %w(-DMRI_ENABLE=0)
defines = %w(-DCHECKSUM_USE_CPP -D__LPC17XX__  -DTARGET_LPC1768 -DWRITE_BUFFER_DISABLE=0 -DSTACK_SIZE=3072 -DCHECKSUM_USE_CPP) +
  exclude_defines.collect{|d| "-DNO_#{d}"} + MRI_DEFINES

defines << '-DNONETWORK' if nonetwork

DEFINES= defines.join(' ')

# Compiler flags used to enable creation of header dependencies.
#DEPFLAGS = -MMD -MP
CFLAGS = '-Wall -Wextra -Wno-unused-parameter -Wcast-align -Wpointer-arith -Wredundant-decls -Wcast-qual -Wcast-align -O2 -g3 -mcpu=cortex-m3 -mthumb -mthumb-interwork -ffunction-sections -fdata-sections  -fno-exceptions -fno-delete-null-pointer-checks'
CPPFLAGS = CFLAGS + ' -fno-rtti -std=gnu++11'

MRI_WRAPS = MRI_ENABLE ? ',--wrap=_read,--wrap=_write,--wrap=semihost_connected' : ''

# Linker script to be used.  Indicates what code should be placed where in memory.
LSCRIPT = "#{MBED_DIR}/LPC1768/GCC_ARM/LPC1768.ld"
LDFLAGS = "-mcpu=cortex-m3 -mthumb -specs=./build/startfile.spec" +
    " -Wl,-Map=#{OBJDIR}/smoothie.map,--cref,--gc-sections,--wrap=_isatty,--wrap=malloc,--wrap=realloc,--wrap=free" +
    MRI_WRAPS +
    " -T#{LSCRIPT}" +
    " -u _scanf_float -u _printf_float"

# tasks

task :clean do
  FileUtils.rm_rf(OBJDIR)
end

task :realclean => [:clean] do
  sh "cd ./mbed;make clean"
end

task :default => [:build]

task :build => [MBED_LIB, :version, "#{PROG}.bin", :size]

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

task :size do
  sh "#{SIZE} #{OBJDIR}/#{PROG}.elf"
end

file MBED_LIB do
  puts "Building Mbed Using Make"
  sh "cd ./mbed;make all"
end

file "#{OBJDIR}/mbed_custom.o" => ['./build/mbed_custom.cpp'] do |t|
  puts "Compiling #{t.source}"
  sh "#{CCPP} #{CPPFLAGS} #{INCLUDE} #{DEFINES} -c -o #{t.name} #{t.prerequisites[0]}"
end

file "#{OBJDIR}/configdefault.o" => 'src/config.default' do |t|
  sh "cd ./src; ../#{OBJCOPY} -I binary -O elf32-littlearm -B arm --readonly-text --rename-section .data=.rodata.configdefault config.default ../#{OBJDIR}/configdefault.o"
end

file "#{PROG}.bin" => ["#{PROG}.elf"] do
  sh "#{OBJCOPY} -O binary #{OBJDIR}/#{PROG}.elf #{OBJDIR}/#{PROG}.bin"
end

file "#{PROG}.elf" => OBJ do |t|
  puts "Linking #{t.source}"
  sh "#{LD} #{LDFLAGS} #{OBJ} #{LIBS}  -o #{OBJDIR}/#{t.name}"
end

#arm-none-eabi-objcopy -R .stack -O ihex ../LPC1768/main.elf ../LPC1768/main.hex
#arm-none-eabi-objdump -d -f -M reg-names-std --demangle ../LPC1768/main.elf >../LPC1768/main.disasm

rule '.o' => lambda{ |objfile| obj2src(objfile, 'cpp') } do |t|
  puts "Compiling #{t.source}"
  sh "#{CCPP} #{CPPFLAGS} #{INCLUDE} #{DEFINES} #{VERSION} -c -o #{t.name} #{t.source}"
end

rule '.o' => lambda{ |objfile| obj2src(objfile, 'c') } do |t|
  puts "Compiling #{t.source}"
  sh "#{CC} #{CFLAGS} #{INCLUDE} #{DEFINES} #{VERSION} -c -o #{t.name} #{t.source}"
end

