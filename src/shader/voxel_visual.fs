R"MULTILINE(

in v4 vP;
in v3 ortho_clip_P;

in v4 world_fP;

out v4 C;

uniform layout(binding = 0, r32ui) uimageBuffer octree_nodes;
uniform layout(binding = 1, r32ui) uimageBuffer octree_diffuse;
uniform u32 octree_level;
uniform u32 octree_resolution;

uniform u32 DEBUG_level;

void main()
{
    v3 coord_01 = 0.5f * ortho_clip_P + v3(0.5f);
    v3 coord = f32(octree_resolution) * coord_01;
    uv3 ucoord = uv3(coord);

    u32 idx = 0;
    u32 node = 0;

    for (u32 level = 0;
         level <= DEBUG_level;
         ++level)
    {
        u32 s = (octree_level - level);
        uv3 sub = ucoord;
        sub >>= s;
        u32 offset = sub.x + (sub.y << 1) + (sub.z << 2);
        sub <<= s;
        ucoord -= sub;
        idx = node + offset;

        node = (imageLoad(octree_nodes, s32(idx)).r & 0x7fffffff);

        if (node == 0)
        {
            break;
        }
    }

    C = v4(rgba8_to_v4(imageLoad(octree_diffuse, s32(idx)).r).rgb, 1);
}

)MULTILINE"
