using Amazon.DynamoDBv2;
using Amazon.DynamoDBv2.Model;
using Amazon.Lambda.APIGatewayEvents;
using Amazon.Lambda.Core;
using System.Text.Json;


// Assembly attribute to enable the Lambda function's JSON input to be converted into a .NET class.
[assembly: LambdaSerializer(typeof(Amazon.Lambda.Serialization.SystemTextJson.DefaultLambdaJsonSerializer))]

namespace WeatherProject;

public class AuthData
{
    public string ApiKey { get; set; }
}
public class WeatherData
{
    public double TemperatureBME { get; set; }
    public double TemperatureDS { get; set; }
    public double Pressure { get; set; }
    public double Humidity { get; set; }
    public int isLive { get; set; }
}
public class InputObject
{
    public WeatherData data { get; set; }
    public AuthData auth { get; set; }
}

public class Function
{
    private static readonly HttpClient _httpClient = new HttpClient();
    /// <summary>
    /// A C# Lambda function handler for an HTTP request triggered by a Function URL.
    /// </summary>
    public async Task<APIGatewayHttpApiV2ProxyResponse> FunctionHandler(APIGatewayHttpApiV2ProxyRequest request, ILambdaContext context)
    {
        // 1. Log the incoming request to verify the trigger source and payload
        context.Logger.LogInformation($"Received HTTP request via Function URL.");
        context.Logger.LogInformation($"Request Body: {request.Body}");

        // 2. Deserialize the JSON body from the HTTP request into your custom object
        InputObject input;
        try
        {
            input = JsonSerializer.Deserialize<InputObject>(request.Body);
        }
        catch (JsonException)
        {
            return new APIGatewayHttpApiV2ProxyResponse
            {
                StatusCode = 400, // Bad Request
                Body = "Invalid request body format."
            };
        }

        if (input is null)
        {
            return new APIGatewayHttpApiV2ProxyResponse
            {
                StatusCode = 400,
                Body = "Request body cannot be empty."
            };
        }

        // 3. Implement your business logic
        if(input.auth is null || !validateApiKey(input.auth.ApiKey))
        {
            return new APIGatewayHttpApiV2ProxyResponse
            {
                StatusCode = 401, // Unauthorized
                Body = "Invalid API key."
            };
        }
        else
        {
            //insert into DynamoDB
            context.Logger.LogInformation("Posting data to DDB");
            AmazonDynamoDBClient client = new AmazonDynamoDBClient();
            string tableName = Environment.GetEnvironmentVariable("ddbtable"); 

            var request2 = new PutItemRequest
            {
                TableName = tableName,
                Item = new Dictionary<string, AttributeValue>()
                      {
                          { "timestamp", new AttributeValue { S = DateTime.UtcNow.ToString() }},
                          {
                            "data",
                            new AttributeValue
                            { S = request.Body }
                          }
                      }
            };
            await client.PutItemAsync(request2);
            //call wunderground API to post data
            if (input.data.isLive == 2)
            {
                // Placeholder for actual API call to Weather Underground
                context.Logger.LogInformation("Posting data to Weather Underground...");

                bool doUpload = true;

                string URL = "http://rtupdate.wunderground.com/weatherstation/updateweatherstation.php?";
                URL += "ID=" + Environment.GetEnvironmentVariable("wuid");
                URL += "&PASSWORD=" + Environment.GetEnvironmentVariable("wupassword");
                URL += "&dateutc=now";
                switch (Environment.GetEnvironmentVariable("tempSensor"))
                {
                    case "BME" :
                        URL += "&tempf=" + CtoF(input.data.TemperatureBME).ToString();
                        URL += "&dewptf=" + DewPtF(input.data.TemperatureBME, input.data.Humidity).ToString();
                        break;
                    case "DS":
                        if(Math.Abs(input.data.TemperatureDS - input.data.TemperatureBME) < int.Parse(Environment.GetEnvironmentVariable("tempdiff")))
                        {
                            URL += "&tempf=" + CtoF(input.data.TemperatureDS).ToString();
                            URL += "&dewptf=" + DewPtF(input.data.TemperatureDS, input.data.Humidity).ToString();
                        }
                        else
                        {
                            context.Logger.LogInformation("DS temp sensor drift too high");
                            doUpload = false;
                        }

                        break;
                }
                URL += "&humidity=" + input.data.Humidity.ToString();
                URL += "&baromin=" + input.data.Pressure.ToString();
                URL += "&softwaretype=DIYESP32&action=updateraw&realtime=1&rtfreq=60";
                if(doUpload)
                {
                    var resp = await _httpClient.GetAsync(URL);
                    string responseContent = await resp.Content.ReadAsStringAsync();
                    context.Logger.LogInformation(resp.StatusCode.ToString() + " " + responseContent);
                }               

            }
        }

        // 4. Create and return an HTTP response
        var responseBody = new
            {
                status = "success",
                message = $"Processed tempBME:'{input.data.TemperatureBME}' tempDS:'{input.data.TemperatureDS}' pres:'{input.data.Pressure}' hum:'{input.data.Humidity}'"
            };

        return new APIGatewayHttpApiV2ProxyResponse
        {
            StatusCode = 200, // OK
            Body = JsonSerializer.Serialize(responseBody),
            Headers = new Dictionary<string, string> { { "Content-Type", "application/json" } }
        };
    }

    public bool validateApiKey(string apiKey)
    {
        // Placeholder for actual API key validation logic
        return apiKey == Environment.GetEnvironmentVariable("apikey"); ;
    }

    public double DewPtF(double tempC, double humi)
    {
        double ans = (tempC - (14.55 + 0.114 * tempC) * (1 - (0.01 * humi)) - Math.Pow(((2.5 + 0.007 * tempC) * (1 - (0.01 * humi))), 3) - (15.9 + 0.117 * tempC) * Math.Pow((1 - (0.01 * humi)), 14));
        return CtoF(ans);
    }
    public double CtoF(double tempC)
    {
        return (tempC * 9 / 5) + 32;
    }
}
