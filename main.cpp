
#include <cstdint>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "filedialog.h"


struct Vec3
{
    uint32_t x, y, z;
};

struct Quad
{
    uint32_t bl, br, tl, tr;
};

int main()
{
    FileDialog::init();

    //load heightmap image
    std::cout << "Please select input heightmap image" << std::endl;

    std::vector<nfdu8filteritem_t> filters = {{ "Heightmap Image", "bmp,jpeg,jpg,png" }};
    std::string inputImagePath = FileDialog::openDialog(filters);
    if (inputImagePath == "") return 0; //file dialog was closed with nothing selected

    int32_t width, height, channels;
    uint8_t* data = stbi_load(inputImagePath.c_str(), &width, &height, &channels, 1);    
    uint32_t numPixels = width * height;

    if (!data)
    {
        std::cout << "failed to load image: " << stbi_failure_reason() << std::endl;
        return -1;
    }

    //get number of layers to create
    std::cout << "Please enter number of layers to divide image heights into (2-255): ";
    std::string input;
    uint32_t numLayers;
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
    std::string outputMeshPath = FileDialog::saveDialog("mesh.obj", filters);
    if (outputMeshPath == "") return 0; //file dialog was closed with nothing selected


    std::cout << "Creating layers" << std::endl;

    //quantise the height to be the layer number it was in the range of
    float interval = 256.0f / numLayers;
    if (dithering)
    {
        std::cout << "Applying dithering" << std::endl;

        //floyd-steinberg
        float* dataFloat = new float[numPixels];
        for (uint32_t p = 0; p < numPixels; p++)
        {
            dataFloat[p] = data[p];
        }

        for (uint32_t p = 0; p < numPixels; p++)
        {
            float oldPixel = dataFloat[p];
            dataFloat[p] = floor(floor(dataFloat[p] / interval) * interval);
            float error = oldPixel - dataFloat[p];

            uint32_t x = p % width;
            uint32_t y = p / width;

            if (x < width - 1) dataFloat[p + 1] += error * 7.0f / 16.0f;
            if (x > 0 && y < height - 1) dataFloat[p + width - 1] += error * 3.0f / 16.0f;
            if (y < height - 1) dataFloat[p + width] += error * 5.0f / 16.0f;
            if (x < width - 1 && y < height - 1) dataFloat[p + width + 1] += error * 1.0f / 16.0f;
        }

        for (uint32_t p = 0; p < numPixels; p++)
        {
            data[p] = fmin(dataFloat[p], 255.0f) / interval;
        }

        delete[] dataFloat;
    }
    else
    {
        for (uint32_t p = 0; p < numPixels; p++)
        {
            data[p] /= interval;
        }
    }

    std::cout << "Creating voxel grid" << std::endl;

    //create voxel grid for each layer and pixel
    numLayers--; //set numLayers to the actual number of layers which will be meshed
    uint32_t elementsPerLayer = numPixels / 8 + (numPixels % 8 != 0);
    std::vector<uint8_t> voxelSolid(numLayers * elementsPerLayer);
    for (uint32_t p = 0; p < numPixels; p++)
    {
        uint32_t y = data[p];
        if (y == 0) continue; //exclude height 0 from output mesh
        
        y--;
        for (uint32_t _y = 0; _y <= y; _y++)
        {
            uint32_t elementIndex = _y * elementsPerLayer + p / 8;
            uint8_t bitIndex = p % 8;
            voxelSolid[elementIndex] |= 1 << bitIndex;
        }
    }

    auto sample = [&](uint32_t x, uint32_t y, uint32_t z) {
        if (x < 0 || x >= width || y < 0 || y >= numLayers || z < 0 || z >= height) return false;

        uint32_t p = (z * width + x);
        uint32_t e = y * elementsPerLayer + p / 8;
        uint8_t b = p % 8;

        return (voxelSolid[e] & (1 << b)) > 0;
    };

    auto voxelHash = [&](uint32_t x, uint32_t y, uint32_t z) {
        if (x >= width || y >= numLayers || z >= height) return -1;
        return (int)(x + z * width + y * numPixels); 
    };
    
    auto voxelHashToPos = [&](uint32_t h) {
        return Vec3 { h % width, h / width / height, (h / width) % height };
    };
    
    //find which quads are visible
    //want quads to be ordered so that taking the first from the map is always the lowest x y z value, hash value increases with x y and z
    std::map<uint32_t, bool> leftQuads;
    std::map<uint32_t, bool> rightQuads;
    std::map<uint32_t, bool> bottomQuads;
    std::map<uint32_t, bool> topQuads;
    std::map<uint32_t, bool> backQuads;
    std::map<uint32_t, bool> frontQuads;

    std::cout << "Finding visible faces" << std::endl;

    for (uint32_t v = 0; v < numPixels * numLayers; v++)
    {
        uint32_t x = v % width;
        uint32_t y = v / numPixels;
        uint32_t z = (v / width) % height;

        if (!sample(x, y, z)) continue; //this voxel is not solid, so cannot have visible faces

        if (!sample(x - 1, y, z)) leftQuads[voxelHash(x, y, z)] = true;
        if (!sample(x + 1, y, z)) rightQuads[voxelHash(x, y, z)] = true;
        if (!sample(x, y - 1, z)) bottomQuads[voxelHash(x, y, z)] = true;
        if (!sample(x, y + 1, z)) topQuads[voxelHash(x, y, z)] = true;
        if (!sample(x, y, z - 1)) backQuads[voxelHash(x, y, z)] = true;
        if (!sample(x, y, z + 1)) frontQuads[voxelHash(x, y, z)] = true;
    }
    
    //merge adjacent quads with greedy meshing
    uint32_t numVertsWide = width + 1;
    uint32_t numVertsHigh = height + 1;
    std::unordered_map<uint32_t, uint32_t> vertIndices; //map from vertex hash to increasing vertex id
    std::vector<Quad> meshQuads;

    auto vertHash = [&](uint32_t x, uint32_t y, uint32_t z) {
        if (x >= numVertsWide || y >= numLayers + 1 || z > numVertsHigh) return -1;
        return (int)(x + z * numVertsWide + y * numVertsWide * numVertsHigh); 
    };

    auto vertHashToPos = [&](uint32_t h) {
        return Vec3 { h % numVertsWide, h / numVertsWide / numVertsHigh, (h / numVertsWide) % numVertsHigh };
    };

    auto createQuad = [&](uint32_t vBL, uint32_t vBR, uint32_t vTL, uint32_t vTR) {
        if (!vertIndices.count(vBL)) vertIndices[vBL] = vertIndices.size();
        if (!vertIndices.count(vBR)) vertIndices[vBR] = vertIndices.size();
        if (!vertIndices.count(vTL)) vertIndices[vTL] = vertIndices.size();
        if (!vertIndices.count(vTR)) vertIndices[vTR] = vertIndices.size();

        meshQuads.push_back({ vertIndices[vBL], vertIndices[vBR], vertIndices[vTL], vertIndices[vTR] });
    };

    std::cout << "Creating faces 1/6" << std::endl;

    while (leftQuads.size() > 0)
    {
        Vec3 pStart = voxelHashToPos(leftQuads.begin()->first); //bottom left of final quad
        uint32_t yEnd = pStart.y;
        uint32_t zEnd = pStart.z;
        leftQuads.erase(leftQuads.begin());

        //grow to the right (positive z)
        zEnd++;
        while (leftQuads.count(voxelHash(pStart.x, pStart.y, zEnd)))
        {
            leftQuads.erase(voxelHash(pStart.x, pStart.y, zEnd));
            zEnd++;
        }

        //grow up (positive y)
        yEnd++;
        while (true)
        {
            bool failed = false;
            for (uint32_t z = pStart.z; z < zEnd; z++)
            {
                if (!leftQuads.count(voxelHash(pStart.x, yEnd, z)))
                {
                    failed = true;
                    break;
                }
            }

            if (failed) break;
            
            for (uint32_t z = pStart.z; z < zEnd; z++)
            {
                leftQuads.erase(voxelHash(pStart.x, yEnd, z));
            }

            yEnd++;
        }

        createQuad(
            vertHash(pStart.x, pStart.y, pStart.z), //bottom left
            vertHash(pStart.x, pStart.y, zEnd),   //bottom right
            vertHash(pStart.x, yEnd, pStart.z),   //top left
            vertHash(pStart.x, yEnd, zEnd)      //top right
        );
    }

    std::cout << "Creating faces 2/6" << std::endl;

    while (rightQuads.size() > 0)
    {
        Vec3 pStart = voxelHashToPos(rightQuads.begin()->first); //bottom right of final quad
        uint32_t yEnd = pStart.y;
        uint32_t zEnd = pStart.z;
        rightQuads.erase(rightQuads.begin());

        //grow to the left (positive z)
        zEnd++;
        while (rightQuads.count(voxelHash(pStart.x, pStart.y, zEnd)))
        {
            rightQuads.erase(voxelHash(pStart.x, pStart.y, zEnd));
            zEnd++;
        }

        //grow up (positive y)
        yEnd++;
        while (true)
        {
            bool failed = false;
            for (uint32_t z = pStart.z; z < zEnd; z++)
            {
                if (!rightQuads.count(voxelHash(pStart.x, yEnd, z)))
                {
                    failed = true;
                    break;
                }
            }

            if (failed) break;
            
            for (uint32_t z = pStart.z; z < zEnd; z++)
            {
                rightQuads.erase(voxelHash(pStart.x, yEnd, z));
            }

            yEnd++;
        }

        pStart.x++; //right-side quads have vertex index 1 higher than voxel index

        createQuad(
            vertHash(pStart.x, pStart.y, zEnd), //bottom left
            vertHash(pStart.x, pStart.y, pStart.z),   //bottom right
            vertHash(pStart.x, yEnd, zEnd),   //top left
            vertHash(pStart.x, yEnd, pStart.z)      //top right
        );
    }

    std::cout << "Creating faces 3/6" << std::endl;

    while (bottomQuads.size() > 0)
    {
        Vec3 pStart = voxelHashToPos(bottomQuads.begin()->first); //bottom left of final quad
        uint32_t xEnd = pStart.x;
        uint32_t zEnd = pStart.z;
        bottomQuads.erase(bottomQuads.begin());

        //grow to the right (positive x)
        xEnd++;
        while (bottomQuads.count(voxelHash(xEnd, pStart.y, pStart.z)))
        {
            bottomQuads.erase(voxelHash(xEnd, pStart.y, pStart.z));
            xEnd++;
        }

        //grow up (positive z)
        zEnd++;
        while (true)
        {
            bool failed = false;
            for (uint32_t x = pStart.x; x < xEnd; x++)
            {
                if (!bottomQuads.count(voxelHash(x, pStart.y, zEnd)))
                {
                    failed = true;
                    break;
                }
            }

            if (failed) break;
            
            for (uint32_t x = pStart.x; x < xEnd; x++)
            {
                bottomQuads.erase(voxelHash(x, pStart.y, zEnd));
            }

            zEnd++;
        }

        createQuad(
            vertHash(pStart.x, pStart.y, pStart.z), //bottom left
            vertHash(xEnd, pStart.y, pStart.z),   //bottom right
            vertHash(pStart.x, pStart.y, zEnd),   //top left
            vertHash(xEnd, pStart.y, zEnd)      //top right
        );
    }

    std::cout << "Creating faces 4/6" << std::endl;

    while (topQuads.size() > 0)
    {
        Vec3 pStart = voxelHashToPos(topQuads.begin()->first); //top left of final quad
        uint32_t xEnd = pStart.x;
        uint32_t zEnd = pStart.z;
        topQuads.erase(topQuads.begin());

        //grow to the right (positive x)
        xEnd++;
        while (topQuads.count(voxelHash(xEnd, pStart.y, pStart.z)))
        {
            topQuads.erase(voxelHash(xEnd, pStart.y, pStart.z));
            xEnd++;
        }

        //grow down (positive z)
        zEnd++;
        while (true)
        {
            bool failed = false;
            for (uint32_t x = pStart.x; x < xEnd; x++)
            {
                if (!topQuads.count(voxelHash(x, pStart.y, zEnd)))
                {
                    failed = true;
                    break;
                }
            }

            if (failed) break;
            
            for (uint32_t x = pStart.x; x < xEnd; x++)
            {
                topQuads.erase(voxelHash(x, pStart.y, zEnd));
            }

            zEnd++;
        }

        pStart.y++; //top quads have vertex index 1 higher than voxel index

        createQuad(
            vertHash(pStart.x, pStart.y, zEnd), //bottom left
            vertHash(xEnd, pStart.y, zEnd),   //bottom right
            vertHash(pStart.x, pStart.y, pStart.z),   //top left
            vertHash(xEnd, pStart.y, pStart.z)      //top right
        );
    }

    std::cout << "Creating faces 5/6" << std::endl;

    while (backQuads.size() > 0)
    {
        Vec3 pStart = voxelHashToPos(backQuads.begin()->first); //bottom right of final quad
        uint32_t xEnd = pStart.x;
        uint32_t yEnd = pStart.y;
        backQuads.erase(backQuads.begin());

        //grow to the left (positive x)
        xEnd++;
        while (backQuads.count(voxelHash(xEnd, pStart.y, pStart.z)))
        {
            backQuads.erase(voxelHash(xEnd, pStart.y, pStart.z));
            xEnd++;
        }

        //grow up (positive y)
        yEnd++;
        while (true)
        {
            bool failed = false;
            for (uint32_t x = pStart.x; x < xEnd; x++)
            {
                if (!backQuads.count(voxelHash(x, yEnd, pStart.z)))
                {
                    failed = true;
                    break;
                }
            }

            if (failed) break;
            
            for (uint32_t x = pStart.x; x < xEnd; x++)
            {
                backQuads.erase(voxelHash(x, yEnd, pStart.z));
            }

            yEnd++;
        }

        createQuad(
            vertHash(xEnd, pStart.y, pStart.z), //bottom left
            vertHash(pStart.x, pStart.y, pStart.z),   //bottom right
            vertHash(xEnd, yEnd, pStart.z),   //top left
            vertHash(pStart.x, yEnd, pStart.z)      //top right
        );
    }

    std::cout << "Creating faces 6/6" << std::endl;

    while (frontQuads.size() > 0)
    {
        Vec3 pStart = voxelHashToPos(frontQuads.begin()->first); //bottom left of final quad
        uint32_t xEnd = pStart.x;
        uint32_t yEnd = pStart.y;
        frontQuads.erase(frontQuads.begin());

        //grow to the right (positive x)
        xEnd++;
        while (frontQuads.count(voxelHash(xEnd, pStart.y, pStart.z)))
        {
            frontQuads.erase(voxelHash(xEnd, pStart.y, pStart.z));
            xEnd++;
        }

        //grow up (positive y)
        yEnd++;
        while (true)
        {
            bool failed = false;
            for (uint32_t x = pStart.x; x < xEnd; x++)
            {
                if (!frontQuads.count(voxelHash(x, yEnd, pStart.z)))
                {
                    failed = true;
                    break;
                }
            }

            if (failed) break;
            
            for (uint32_t x = pStart.x; x < xEnd; x++)
            {
                frontQuads.erase(voxelHash(x, yEnd, pStart.z));
            }

            yEnd++;
        }

        pStart.z++; //front quads have vertex index 1 higher than voxel index

        createQuad(
            vertHash(pStart.x, pStart.y, pStart.z), //bottom left
            vertHash(xEnd, pStart.y, pStart.z),   //bottom right
            vertHash(pStart.x, yEnd, pStart.z),   //top left
            vertHash(xEnd, yEnd, pStart.z)      //top right
        );
    }
    
    std::cout << "Creating vertices" << std::endl;

    //create list of vertex positions at the index used by the quads
    std::vector<Vec3> vertPositions(vertIndices.size());
    for (const std::pair<const uint32_t, uint32_t>& i : vertIndices)
    {
        vertPositions[i.second] = vertHashToPos(i.first);
    }

    //write vertex positions and quad triangles to obj
    std::ofstream meshFile;
    meshFile.open(outputMeshPath);
    std::cout << "Writing vertices" << std::endl;

    //set model size so that the larger horizontal dimension is 1 unit (and the other scales to keep aspect ratio), and each vertical layer is 1 unit
    //this makes it easy to later scale the layers and model width to be whatever size is desired
    float scale = (width > height) ? 1.0f / width : 1.0f / height;
    for (const Vec3& v : vertPositions)
    {
        meshFile << "v " << v.x * scale << " " << v.y << " " << v.z * scale << std::endl;
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
