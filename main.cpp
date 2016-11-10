#include <iostream>
#include <iomanip>
#include <fstream>
#include <setjmp.h>
#include <png.h>
#include <stdio.h>

#ifdef WIN32
#define FOLDER_SEPARATOR '\\'
#else
#define FOLDER_SEPARATOR '/'
#endif

class Converter {

private:
    const int16_t MIN_INT16 = -32768;

    char *buffer = NULL;
    int pointer = 0;
    bool isTerragenFile = false;
    bool isTerrainFile = false;
    bool hasSize = false;
    int size = 0;
    int xpts = 0;
    int ypts = 0;
    int crvm = 0;
    float scaleX = 0;
    float scaleY = 0;
    float scaleZ = 0;
    float crad = 0;
    int heightScale = 0;
    int baseHeight = 0;
    int bufferSize = 0;
    const char *inFileName;
    const char *outFileName;

    bool saveRaw = true;
    bool savePng = true;

public:
    int convert(const char *fileName, const char *outfile) {

        inFileName = fileName;
        outFileName = outfile;

        std::ifstream ifs(fileName, std::ios::binary);

        if (!ifs.is_open()) {
            std::cerr << "Unable to read file: " << fileName << std::endl;
            return 1;
        }

        try {
            ifs.seekg(0, std::ios::end);
            std::streampos length = ifs.tellg();
            if (length == 0) {
                std::cerr << "File is empty: " << fileName << std::endl;
                return 1;
            }

            bufferSize = (int) length;
            buffer = new char[bufferSize];
            pointer = 0;

            ifs.seekg(0);
            ifs.read(buffer, bufferSize);
            ifs.close();

            while (readChunk()); // nop

        } catch (std::exception &e) {
            std::cerr << e.what() << std::endl;
            return 2;
        }

        return 0;
    }

    void setSaveRaw(bool saveRaw) {
        Converter::saveRaw = saveRaw;
    }

    void setSavePng(bool savePng) {
        Converter::savePng = savePng;
    }

private:

    std::string withoutExtension(const char *name) {
        const char *pos = strrchr(name, '.');
        if (pos == NULL)
            pos = name + strlen(name);
        std::string fname(name, pos - name);
        return fname;
    }

    std::string withoutPath(const char *name) {
        const char *pos = strrchr(name, FOLDER_SEPARATOR);
        if (pos == NULL)
            return name;
        if (pos == name + strlen(name))
            throw new std::runtime_error("Output file name is directory");
        std::string fname(pos + 1, strlen(pos + 1));
        return fname;
    }

    bool readChunk() {
        if (pointer >= bufferSize)
            return false;

        int markerLength = isTerrainFile ? 4 : 8;
        char *marker = buffer + pointer;
        pointer += markerLength;

        if (!isTerragenFile && !memcmp(marker, "TERRAGEN", 8)) {
            isTerragenFile = true;
        } else if (isTerragenFile && !memcmp(marker, "TERRAIN ", 8)) {
            isTerrainFile = true;
            log("Reading Terragen file");
        } else if (isTerrainFile && !memcmp(marker, "SIZE", 4)) {
            hasSize = true;
            size = readInt16();
            readPadding(2);

            xpts = size + 1;
            ypts = size + 1;
        } else if (hasSize && !memcmp(marker, "XPTS", 4)) {
            xpts = readInt16();
            readPadding(2);
        } else if (hasSize && !memcmp(marker, "YPTS", 4)) {
            ypts = readInt16();
            readPadding(2);
        } else if (hasSize && !memcmp(marker, "SCAL", 4)) {
            scaleX = readFloat4();
            scaleY = readFloat4();
            scaleZ = readFloat4();
        } else if (hasSize && !memcmp(marker, "CRAD", 4)) {
            crad = readFloat4();
        } else if (hasSize && !memcmp(marker, "CRVM", 4)) {
            crvm = readInt32();
        } else if (hasSize && !memcmp(marker, "ALTW", 4)) {

            std::cout << "Terrain size: " << xpts << "x" << ypts << std::endl;

            heightScale = readInt16();
            baseHeight = readInt16();

//            for(int i = 0; i < xpts*ypts; i++) {
//                int elevation = readInt16();
//            }


            {
                size_t count = static_cast<size_t >(2 * xpts * ypts);
                std::string outFname;
                if (outFileName != NULL)
                    outFname = outFileName;
                else
                    outFname = withoutExtension(withoutPath(inFileName).c_str()) + ".raw";

                int16_t *intptr = reinterpret_cast<int16_t *>(buffer + pointer);
                for (int i = 0; i < count / 2; i++) {
                    // Transpose signed 16-bit integer range to unsigned 16-bit integer
                    intptr[i] = static_cast<u_int16_t>((int32_t) intptr[i] - MIN_INT16);
                }

                if (saveRaw) {
                    std::ofstream ofs(outFname, std::ios::out | std::ios::binary);
                    if (!ofs.is_open()) {
                        throw std::runtime_error(std::string("Unable to write file: ") + outFname);
                    }

                    ofs.write(buffer + pointer, count);
                    ofs.flush();
                    ofs.close();
                }

                if (savePng) {
                    std::string pngFname = withoutExtension(outFname.c_str()) + ".png";
                    FILE *file = fopen(pngFname.c_str(), "wb");
                    save_png(file, xpts, ypts, intptr);
                }

                pointer += count;
            }

            std::cout << "Successfully read " << xpts * ypts << " points" << std::endl;
        } else if (!memcmp(marker, "EOF ", 4)) {
            // end of file
            return false;
        } else {
            throw std::runtime_error("Unable to read file: Unexpected chunk");
        }

        return true;
    }

    void save_png(FILE *fp, int width, int height, int16_t *data) {
        png_structp png_ptr = NULL;
        png_infop info_ptr = NULL;
        size_t x, y;
        png_bytepp row_pointers;

        png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        if (png_ptr == NULL) {
            return;
        }

        info_ptr = png_create_info_struct(png_ptr);
        if (info_ptr == NULL) {
            png_destroy_write_struct(&png_ptr, NULL);
            return;
        }

        if (setjmp(png_jmpbuf(png_ptr))) {
            png_destroy_write_struct(&png_ptr, &info_ptr);
            return;
        }

        png_set_IHDR(png_ptr, info_ptr,
                     (png_uint_32) width, (png_uint_32) height, // width and height
                     16, // bit depth
                     PNG_COLOR_TYPE_GRAY, // color type
                     PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

        /* Initialize rows of PNG. */
        row_pointers = (png_bytepp) png_malloc(png_ptr,
                                               height * sizeof(png_bytep));

        for (int i = 0; i < height; i++)
            row_pointers[i] = (png_bytep) png_malloc(png_ptr, static_cast<unsigned int>(width * 2));

        //set row data
        for (y = 0; y < height; ++y) {
            png_bytep row = row_pointers[y];
            for (x = 0; x < width; ++x) {
                // flip vertical
                int16_t color = data[(height - y) * width + x];
                *row++ = (png_byte) (color >> 8);
                *row++ = (png_byte) (color & 0xFF);
            }
        }

        /* Actually write the image data. */
        png_init_io(png_ptr, fp);
        png_set_rows(png_ptr, info_ptr, row_pointers);
        png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);
        //png_write_image(png_ptr, row_pointers);

        /* Cleanup. */
        for (y = 0; y < height; y++) {
            png_free(png_ptr, row_pointers[y]);
        }
        png_free(png_ptr, row_pointers);
        png_destroy_write_struct(&png_ptr, &info_ptr);
    }

    int readInt32() {
        int num = *(reinterpret_cast<int32_t *>(buffer + pointer));
        pointer += 4;
        return num;
    }

    float readFloat4() {
        float num = *(reinterpret_cast<float *>(buffer + pointer));
        pointer += 4;
        return num;
    }

    void readPadding(int num) {
        pointer += num;
    }

    int readInt16() {
        int num = *(reinterpret_cast<int16_t *>(buffer + pointer));
        pointer += 2;
        return num;
    }

    void log(std::string message) {
        std::cout << message << std::endl;
    }
};

class CmdLineParser {
private:
    bool savePng;
    bool saveRaw;
    bool parsed;
    std::string inFile;
    std::string outFile;

    void usage() {
        std::cout << "Usage ter2raw infile.ter [outfile.raw]" << std::endl;
    }

public:
    CmdLineParser(int argc, char **argv) {
        savePng = false;
        saveRaw = false;
        inFile = "";
        outFile = "";
        parsed = false;

        if (argc == 1)
            usage();

        for (int i = 1; i < argc; i++) {
            std::string param(argv[i]);
            trim(param);

            if (!param.compare("-p"))
                savePng = true;
            else if (!param.compare("-r"))
                saveRaw = true;
            else if (inFile.length() == 0) {
                inFile = param;
                parsed = true;
            } else if (outFile.length() == 0)
                outFile = param;
            else {
                usage();
                parsed = false;
                break;
            }
        }
    }

    bool isSavePng() const {
        return savePng;
    }

    bool isSaveRaw() const {
        return saveRaw || !savePng;
    }

    bool isParsed() const {
        return parsed;
    }

    const char *getInFile() const {
        return inFile.c_str();
    }

    const char *getOutFile() const {
        return outFile.length() > 0 ? outFile.c_str() : NULL;
    }

private:
    // trim from start
    static inline std::string &ltrim(std::string &s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(),
                                        std::not1(std::ptr_fun<int, int>(std::isspace))));
        return s;
    }

    // trim from end
    static inline std::string &rtrim(std::string &s) {
        s.erase(std::find_if(s.rbegin(), s.rend(),
                             std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
        return s;
    }

    // trim from both ends
    static inline std::string &trim(std::string &s) {
        return ltrim(rtrim(s));
    }
};

int main(int argc, char **argv) {
    CmdLineParser parser(argc, argv);
    if (!parser.isParsed())
        return 1;

    Converter converter;
    converter.setSavePng(parser.isSavePng());
    converter.setSaveRaw(parser.isSaveRaw());
    return converter.convert(parser.getInFile(), parser.getOutFile());
}