/* For each DXBC file: which chunks do D3DGetBlobPart's signature parts return, and what does
 * D3DReflect (ID3D11 and ID3D12 interfaces) report for inputs, outputs and patch constants? */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <d3d12shader.h>

typedef HRESULT (WINAPI *PFN_PART)(const void *, SIZE_T, D3D_BLOB_PART, UINT, ID3DBlob **);
typedef HRESULT (WINAPI *PFN_R)(const void *, SIZE_T, REFIID, void **);

static void chunks(const char *what, ID3DBlob *b)
{
    const unsigned char *d = b->lpVtbl->GetBufferPointer(b);
    unsigned int n = *(const unsigned int *)(d + 28), i;
    printf("   %s: %u bytes, chunks:", what, (unsigned)b->lpVtbl->GetBufferSize(b));
    for (i = 0; i < n; i++) { unsigned int off = *(const unsigned int *)(d + 32 + 4 * i); printf(" %.4s(%u)", d + off, *(const unsigned int *)(d + off + 4)); }
    printf("\n");
}

int main(int argc, char **argv)
{
    HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
    PFN_PART part = (PFN_PART)GetProcAddress(m, "D3DGetBlobPart");
    PFN_R rf = (PFN_R)GetProcAddress(m, "D3DReflect");
    static const struct { D3D_BLOB_PART p; const char *n; } parts[] = {
        { D3D_BLOB_INPUT_SIGNATURE_BLOB, "input" }, { D3D_BLOB_OUTPUT_SIGNATURE_BLOB, "output" },
        { D3D_BLOB_INPUT_AND_OUTPUT_SIGNATURE_BLOB, "in+out" }, { D3D_BLOB_PATCH_CONSTANT_SIGNATURE_BLOB, "patch" },
        { D3D_BLOB_ALL_SIGNATURE_BLOB, "all" },
    };
    int i; unsigned int j, k;

    for (i = 1; i < argc; i++)
    {
        void *data; long size; FILE *f = fopen(argv[i], "rb");
        ID3D11ShaderReflection *r11; ID3D12ShaderReflection *r12;
        HRESULT hr;

        fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
        data = malloc(size); fread(data, 1, size, f); fclose(f);
        printf("== %s\n", argv[i]);
        for (j = 0; j < ARRAYSIZE(parts); j++)
        {
            ID3DBlob *b = NULL;
            hr = part(data, size, parts[j].p, 0, &b);
            if (FAILED(hr)) printf("   %s: hr %#lx\n", parts[j].n, hr);
            else { chunks(parts[j].n, b); b->lpVtbl->Release(b); }
        }
        if (SUCCEEDED(hr = rf(data, size, &IID_ID3D11ShaderReflection, (void **)&r11)))
        {
            D3D11_SHADER_DESC sd; r11->lpVtbl->GetDesc(r11, &sd);
            printf("   d3d11 reflect: in %u out %u patch %u\n", sd.InputParameters, sd.OutputParameters, sd.PatchConstantParameters);
            for (k = 0; k < sd.InputParameters + sd.OutputParameters + sd.PatchConstantParameters; k++)
            {
                D3D11_SIGNATURE_PARAMETER_DESC p; const char *kind;
                if (k < sd.InputParameters) { kind = "in "; r11->lpVtbl->GetInputParameterDesc(r11, k, &p); }
                else if (k < sd.InputParameters + sd.OutputParameters) { kind = "out"; r11->lpVtbl->GetOutputParameterDesc(r11, k - sd.InputParameters, &p); }
                else { kind = "pc "; r11->lpVtbl->GetPatchConstantParameterDesc(r11, k - sd.InputParameters - sd.OutputParameters, &p); }
                printf("     %s %s%u reg %u sv %u comp %u mask %#x rw %#x stream %u minprec %u\n", kind, p.SemanticName, p.SemanticIndex,
                        p.Register, p.SystemValueType, p.ComponentType, p.Mask, p.ReadWriteMask, p.Stream, p.MinPrecision);
            }
            r11->lpVtbl->Release(r11);
        }
        else printf("   d3d11 reflect hr %#lx\n", hr);
        if (SUCCEEDED(hr = rf(data, size, &IID_ID3D12ShaderReflection, (void **)&r12)))
        {
            D3D12_SHADER_DESC sd; r12->lpVtbl->GetDesc(r12, &sd);
            printf("   d3d12 reflect: in %u out %u patch %u\n", sd.InputParameters, sd.OutputParameters, sd.PatchConstantParameters);
            for (k = 0; k < sd.InputParameters; k++)
            {
                D3D12_SIGNATURE_PARAMETER_DESC p; r12->lpVtbl->GetInputParameterDesc(r12, k, &p);
                printf("     in  %s%u reg %u sv %u comp %u mask %#x rw %#x stream %u minprec %u\n", p.SemanticName, p.SemanticIndex,
                        p.Register, p.SystemValueType, p.ComponentType, p.Mask, p.ReadWriteMask, p.Stream, p.MinPrecision);
            }
            r12->lpVtbl->Release(r12);
        }
        else printf("   d3d12 reflect hr %#lx\n", hr);
    }
    return 0;
}
