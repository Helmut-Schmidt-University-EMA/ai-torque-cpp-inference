import asyncio
import json
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def test_context7():
    print("🚀 Starting Context7 MCP Demo...\n")
    
    # Configure the server connection
    server_params = StdioServerParameters(
        command="npx",
        args=["-y", "@upstash/context7-mcp"],
        # Optional: Add your API key for higher rate limits
        # env={"CONTEXT7_API_KEY": "your-api-key-here"}
    )
    
    async with stdio_client(server_params) as (read, write):
        async with ClientSession(read, write) as session:
            # Initialize the connection
            await session.initialize()
            print("✅ Connected to Context7 MCP server\n")
            
            # List available tools
            print("📋 Available tools:")
            tools = await session.list_tools()
            for tool in tools.tools:
                print(f"  - {tool.name}: {tool.description}")
            print()
            
            # Test 1: Resolve a library name to Context7 ID
            print("🔍 Test 1: Resolving library name 'express'...")
            resolve_result = await session.call_tool(
                "resolve-library-id",
                arguments={"libraryName": "express"}
            )
            print(f"Result: {json.dumps(resolve_result.model_dump(), indent=2)}")
            print()
            
            # Test 2: Get documentation for Express.js
            print("📚 Test 2: Getting Express.js documentation...")
            docs_result = await session.call_tool(
                "get-library-docs",
                arguments={
                    "context7CompatibleLibraryID": "/expressjs/express",
                    "topic": "routing",
                    "tokens": 2000
                }
            )
            
            print("Documentation preview:")
            if docs_result.content:
                content = docs_result.content[0]
                if hasattr(content, 'text'):
                    preview = content.text[:500]
                    print(f"{preview}...\n")
            print()
            
            # Test 3: Get Python FastAPI documentation
            print("📚 Test 3: Getting FastAPI documentation...")
            fastapi_docs = await session.call_tool(
                "get-library-docs",
                arguments={
                    "context7CompatibleLibraryID": "/tiangolo/fastapi",
                    "topic": "dependencies",
                    "tokens": 1500
                }
            )
            
            print("FastAPI Documentation preview:")
            if fastapi_docs.content:
                content = fastapi_docs.content[0]
                if hasattr(content, 'text'):
                    preview = content.text[:500]
                    print(f"{preview}...\n")
            
            # Test 4: Resolve and fetch in one go
            print("🔍 Test 4: Resolving 'mongodb' and fetching docs...")
            
            # First resolve
            mongo_resolve = await session.call_tool(
                "resolve-library-id",
                arguments={"libraryName": "mongodb"}
            )
            print(f"Resolved: {json.dumps(mongo_resolve.model_dump(), indent=2)[:200]}...")
            
            # Then get docs
            mongo_docs = await session.call_tool(
                "get-library-docs",
                arguments={
                    "context7CompatibleLibraryID": "/mongodb/docs",
                    "topic": "connection",
                    "tokens": 1000
                }
            )
            
            print("MongoDB Documentation preview:")
            if mongo_docs.content:
                content = mongo_docs.content[0]
                if hasattr(content, 'text'):
                    preview = content.text[:400]
                    print(f"{preview}...\n")
            
            print("✨ All tests completed successfully!")

if __name__ == "__main__":
    try:
        asyncio.run(test_context7())
        print("\n🔌 Disconnected from Context7 MCP server")
    except Exception as e:
        print(f"❌ Error: {e}")
        import traceback
        traceback.print_exc()