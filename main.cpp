
#include <iostream>
#include <fstream>
#include <map>
#include <unordered_map>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "filedialog.h"


struct Vec3
{
    int x, y, z;

    void operator+=(const Vec3& rhs);
};

struct Quad
{
    int bl, br, tl, tr;
};

Vec3 operator+(const Vec3& lhs, const Vec3& rhs)
{
    return Vec3 { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
}

Vec3 operator*(const int& lhs, const Vec3& rhs)
{
    return Vec3 { lhs * rhs.x, lhs * rhs.y, lhs * rhs.z };
}

void Vec3::operator+=(const Vec3& rhs)
{
    *this = *this + rhs;
}

int main()
{
    FileDialog::init();

    //load heightmap image
    std::cout << "Select input image" << std::endl;

    std::vector<nfdu8filteritem_t> filters = {{ "Heightmap Image", "bmp,jpeg,jpg,png,BMP,JPEG,JPG,PNG" }};
    std::string inputImagePath = FileDialog::openDialog(filters);
    if (inputImagePath == "") return 0; //file dialog was closed with nothing selected

    int32_t imageWidth, imageHeight, channels;
    uint8_t* data = stbi_load(inputImagePath.c_str(), &imageWidth, &imageHeight, &channels, 1);    
    int numPixels = imageWidth * imageHeight;

    if (!data)
    {
        std::cout << "failed to load image: " << stbi_failure_reason() << std::endl;
        return -1;
    }

    std::cout << "Using image: " << inputImagePath << std::endl;

    //get number of layers to create
    std::cout << "Number of colours/layers (2-255): ";
    std::string input;
    int numLayers;
    while (true)
    {
        std::cin >> input;

        try
        {
            numLayers = std::stoul(input);
            if (numLayers < 2) throw std::invalid_argument("Number of layers cannot be less than 2");
            if (numLayers > 255) throw std::invalid_argument("Number of layers cannot be more than 255");
        }
        catch (std::invalid_argument e)
        {
            std::cout << "input not valid: " << input << std::endl
                << "Please enter number between 2 and 255: ";
            continue;
        }

        break;
    }

    //get layer height
    std::cout << "Layer height (mm): ";
    float layerHeight;
    while (true)
    {
        std::cin >> input;

        try
        {
            layerHeight = std::stof(input);
            if (layerHeight <= 0.0f) throw std::invalid_argument("Invalid layer height");
        }
        catch (std::invalid_argument e)
        {
            std::cout << "input not valid: " << input << std::endl
                << "Please enter number greater than 0: ";
            continue;
        }

        break;
    }

    //get model width height
    std::cout << "Model width (mm) (depth will be scaled to keep image aspect ratio): ";
    float widthScale;
    while (true)
    {
        std::cin >> input;

        try
        {
            widthScale = std::stof(input);
            if (widthScale <= 0.0f) throw std::invalid_argument("Invalid model width scale");
        }
        catch (std::invalid_argument e)
        {
            std::cout << "input not valid: " << input << std::endl
                << "Please enter number greater than 0: ";
            continue;
        }

        break;
    }

    std::cout << "Enable dithering for less colour banding (warning: makes mesh file size very big) (y/n): ";
    bool dithering = true;
    while (true)
    {
        std::cin >> input;
        if (input == "y" || input == "Y") break;
        if (input == "n" || input == "N")
        {
            dithering = false;
            break;
        }

        std::cout << "Please type y or n" << std::endl;
    }

    std::cout << "Please select where to save output mesh" << std::endl;

    filters = {{ "OBJ Mesh", "obj" }};

#ifdef _WIN32
    size_t sub1 = inputImagePath.find_last_of('\\') + 1;
#else
    size_t sub1 = inputImagePath.find_last_of('/') + 1;
#endif
    size_t sub2 = inputImagePath.find_last_of('.');
    std::string defaultFileName = inputImagePath.substr(sub1, sub2 - sub1) + ".obj";
    std::string outputMeshPath = FileDialog::saveDialog(defaultFileName, filters);
    if (outputMeshPath == "") return 0; //file dialog was closed with nothing selected

    std::cout << "Saving obj to: " << outputMeshPath << std::endl;

    std::cout << "Creating layers" << std::endl;

    //quantise the height to be the layer number it was in the range of
    float interval = 256.0f / numLayers;

    if (dithering)
    {
        std::cout << "Applying dithering" << std::endl;

        //floyd-steinberg
        float* dataFloat = new float[numPixels];
        memset(dataFloat, 0.0f, numPixels * sizeof(float));

        for (int p = 0; p < numPixels; p++)
        {
            //set float version of pixel data to the current pixel
            //add instead of just assign so that accumulated errors from previous pixels can be kept
            dataFloat[p] += data[p];
            
            //quantise pixel and get error
            float oldPixel = dataFloat[p];
            dataFloat[p] = floor(floor(dataFloat[p] / interval) * interval);
            float error = oldPixel - dataFloat[p];

            //diffuse error through neighbouring pixels
            int x = p % imageWidth;
            int y = p / imageWidth;

            if (x < imageWidth - 1) dataFloat[p + 1] += error * 7.0f / 16.0f;
            if (x > 0 && y < imageHeight - 1) dataFloat[p + imageWidth - 1] += error * 3.0f / 16.0f;
            if (y < imageHeight - 1) dataFloat[p + imageWidth] += error * 5.0f / 16.0f;
            if (x < imageWidth - 1 && y < imageHeight - 1) dataFloat[p + imageWidth + 1] += error * 1.0f / 16.0f;

            //write final layer number
            data[p] = dataFloat[p] / interval;
        }

        delete[] dataFloat;
    }
    else
    {
        //no dithering, just quantise the pixel by dividing and flooring
        for (int p = 0; p < numPixels; p++)
        {
            data[p] /= interval;
        }
    }

    std::cout << "Creating voxel grid" << std::endl;

    //create voxel grid
    int elementsPerLayer = numPixels / 8 + (numPixels % 8 != 0);
    std::vector<uint8_t> voxelSolid(numLayers * elementsPerLayer);
    
    for (int p = 0; p < numPixels; p++)
    {
        int y = data[p];
        for (int _y = 0; _y <= y; _y++)
        {
            int elementIndex = _y * elementsPerLayer + p / 8;
            uint8_t bitIndex = p % 8;
            voxelSolid[elementIndex] |= 1 << bitIndex;
        }
    }

    auto sample = [&](int x, int y, int z) {
        if (x < 0 || x >= imageWidth || y < 0 || y >= numLayers || z < 0 || z >= imageHeight) return false;

        int p = (z * imageWidth + x);
        int e = y * elementsPerLayer + p / 8;
        uint8_t b = p % 8;

        return (voxelSolid[e] & (1 << b)) > 0;
    };

    auto voxelHash = [&](int x, int y, int z) {
        if (x < 0 || x >= imageWidth || y < 0 || y >= numLayers || z < 0 || z >= imageHeight) return -1;
        return (int)(x + y * imageWidth + z * imageWidth * numLayers); 
    };
    
    auto voxelHashToPos = [&](int h) {
        return Vec3 { h % imageWidth, (h / imageWidth) % numLayers, h / imageWidth / numLayers};
    };
    
    //find which quads are visible
    //want quads to be ordered so that taking the first from the map is always the lowest x y z value, hash value increases with x y and z
    std::map<int, bool> leftQuads;
    std::map<int, bool> rightQuads;
    std::map<int, bool> bottomQuads;
    std::map<int, bool> topQuads;
    std::map<int, bool> backQuads;
    std::map<int, bool> frontQuads;

    std::cout << "Finding visible faces" << std::endl;

    for (int v = 0; v < numPixels * numLayers; v++)
    {
        int x = v % imageWidth;
        int y = (v / imageWidth) % numLayers;
        int z = v / imageWidth / numLayers;

        if (!sample(x, y, z)) continue; //this voxel is not solid, so cannot have visible faces

        int vh = voxelHash(x, y, z);
        if (vh == -1)
        {
            throw std::invalid_argument("Invalid position for voxelHash: " + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z));
        }

        //if there is no voxel blocking it, then that face should be visible
        //use of "true" is just to create an entry in the map, don't want any false entries
        //using map for sorting and easy checking if value exists with count()
        if (!sample(x - 1, y, z)) leftQuads[vh] = true;
        if (!sample(x + 1, y, z)) rightQuads[vh] = true;
        if (!sample(x, y - 1, z)) bottomQuads[vh] = true;
        if (!sample(x, y + 1, z)) topQuads[vh] = true;
        if (!sample(x, y, z - 1)) backQuads[vh] = true;
        if (!sample(x, y, z + 1)) frontQuads[vh] = true;
    }
    
    //merge adjacent quads with greedy meshing
    int numVertsWide = imageWidth + 1;
    int numVertsHigh = numLayers + 1;
    int numVertsDeep = imageHeight + 1;
    std::unordered_map<int, int> vertIndices; //map from vertex hash to increasing vertex id
    std::vector<Quad> meshQuads;

    auto vertHash = [&](int x, int y, int z) {
        if (x < 0 || x >= numVertsWide || y < 0 || y >= numVertsHigh || z < 0 || z >= numVertsDeep)
        {
            throw std::invalid_argument("Invalid position for vertHash: " + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) +
                " - max valid is " + std::to_string(numVertsWide) + ", " + std::to_string(numVertsHigh) + ", " + std::to_string(numVertsDeep));
        }
        return x + y * numVertsWide + z * numVertsWide * numVertsHigh; 
    };

    auto vertHashToPos = [&](int h) {
        return Vec3 { h % numVertsWide, (h / numVertsWide) % numVertsHigh, h / numVertsWide / numVertsHigh };
    };

    auto createQuad = [&](int vBL, int vBR, int vTL, int vTR) {
        if (!vertIndices.count(vBL)) vertIndices[vBL] = vertIndices.size();
        if (!vertIndices.count(vBR)) vertIndices[vBR] = vertIndices.size();
        if (!vertIndices.count(vTL)) vertIndices[vTL] = vertIndices.size();
        if (!vertIndices.count(vTR)) vertIndices[vTR] = vertIndices.size();

        meshQuads.push_back({ vertIndices[vBL], vertIndices[vBR], vertIndices[vTL], vertIndices[vTR] });
    };

    auto greedy = [&](std::map<int, bool> quads, Vec3 dirHorizontal, Vec3 dirVertical, Vec3 dirFace) {
        while (quads.size() > 0)
        {
            //get starting position, which will be left-bottom-back-most corner of quad
            Vec3 pStart = voxelHashToPos(quads.begin()->first);
            quads.erase(quads.begin());

            //grow horizontally in quad plane
            Vec3 pTemp = pStart + dirHorizontal;
            int hCount = 1;
            while (quads.count(voxelHash(pTemp.x, pTemp.y, pTemp.z)))
            {
                quads.erase(voxelHash(pTemp.x, pTemp.y, pTemp.z));
                pTemp += dirHorizontal;
                hCount++;
            }
            
            //grow vertically in quad plane
            int vCount = 1;
            while (true)
            {
                bool failed = false;
                for (int h = 0; h < hCount; h++)
                {
                    Vec3 p = pStart + h * dirHorizontal + vCount * dirVertical;
                    if (!quads.count(voxelHash(p.x, p.y, p.z)))
                    {
                        failed = true;
                        break;
                    }
                }

                if (failed) break;
                
                for (int h = 0; h < hCount; h++)
                {
                    Vec3 p = pStart + h * dirHorizontal + vCount * dirVertical;
                    quads.erase(voxelHash(p.x, p.y, p.z));
                }

                pTemp += dirVertical;
                vCount++;
            }

            //right, top and front quads have vertex index 1 higher than voxel index in x, y and z respectively
            if (dirFace.x == 1 || dirFace.y == 1 || dirFace.z == 1) pStart += dirFace;

            //create points for each corner of quad
            Vec3 pBL = pStart;
            Vec3 pBR = pStart + hCount * dirHorizontal;
            Vec3 pTL = pStart + vCount * dirVertical;
            Vec3 pTR = pStart + hCount * dirHorizontal + vCount * dirVertical;

            //re-order corners to be correct for certain faces
            //right side, pStart is bottom right corner
            if (dirFace.x == 1)
            {
                std::swap(pBL, pBR);
                std::swap(pTL, pTR);
            }

            //top side, pStart is top left corner
            if (dirFace.y == 1)
            {
                std::swap(pBL, pTL);
                std::swap(pBR, pTR);
            }

            //back side, pStart is bottom right
            if (dirFace.z == -1)
            {
                std::swap(pBL, pBR);
                std::swap(pTL, pTR);
            }

            createQuad(
                vertHash(pBL.x, pBL.y, pBL.z),  //bottom left
                vertHash(pBR.x, pBR.y, pBR.z),  //bottom right
                vertHash(pTL.x, pTL.y, pTL.z),  //top left
                vertHash(pTR.x, pTR.y, pTR.z)   //top right
            );
        }
    };

    std::cout << "Creating faces 1/6" << std::endl;
    greedy(leftQuads, {0, 0, 1}, {0, 1, 0}, {-1, 0, 0});
    std::cout << "Creating faces 2/6" << std::endl;
    greedy(rightQuads, {0, 0, 1}, {0, 1, 0}, {1, 0, 0});
    std::cout << "Creating faces 3/6" << std::endl;
    greedy(bottomQuads, {1, 0, 0}, {0, 0, 1}, {0, -1, 0});
    std::cout << "Creating faces 4/6" << std::endl;
    greedy(topQuads, {1, 0, 0}, {0, 0, 1}, {0, 1, 0});
    std::cout << "Creating faces 5/6" << std::endl;
    greedy(backQuads, {1, 0, 0}, {0, 1, 0}, {0, 0, -1});
    std::cout << "Creating faces 6/6" << std::endl;
    greedy(frontQuads, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});

    
    std::cout << "Creating vertices" << std::endl;

    //create list of vertex positions at the index used by the quads
    std::vector<Vec3> vertPositions(vertIndices.size());
    for (const std::pair<const int, int>& i : vertIndices)
    {
        vertPositions[i.second] = vertHashToPos(i.first);
    }

    //write vertex positions and quad triangles to obj
    std::ofstream meshFile;
    meshFile.open(outputMeshPath);
    std::cout << "Writing vertices" << std::endl;

    //set model size so that the larger horizontal dimension is 1 unit (and the other scales to keep aspect ratio), and each vertical layer is 1 unit
    //this makes it easy to later scale the layers and model width to be whatever size is desired
    float scale = widthScale / imageWidth;
    for (const Vec3& v : vertPositions)
    {
        meshFile << "v " << v.x * scale << " " << v.y * layerHeight << " " << v.z * scale << std::endl;
    }
    
    meshFile << std::endl;

    std::cout << "Writing triangles" << std::endl;
    for (const Quad& q : meshQuads)
    {
        meshFile << "f " << q.tl + 1 << " " << q.bl + 1 << " " << q.br + 1 << std::endl
                 << "f " << q.br + 1 << " " << q.tr + 1 << " " << q.tl + 1 << std::endl;
    }
    meshFile.close();


    stbi_image_free(data);
    FileDialog::destroy();

    return 0;
}